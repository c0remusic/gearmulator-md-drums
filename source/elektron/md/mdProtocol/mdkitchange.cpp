#include "mdkitchange.h"

#include "mdsysexcodec.h"

#include <algorithm>

namespace md::automation::sysex
{
	namespace
	{
		using Kind = KitChange::Kind;

		constexpr uint8_t g_assignMachine = 0x5b;
		constexpr uint8_t g_masterEffect = 0x5d;	// to $60, in MasterEffect order
		constexpr uint8_t g_lfoChange = 0x62;
		constexpr uint8_t g_linkChange = 0x65;		// SET TRIG GROUP
		constexpr uint8_t g_chokeChange = 0x66;		// SET MUTE GROUP
		constexpr uint8_t g_offOnTheWire = 0x7f;	// MdKit::Off as $65 and $66 carry it
		constexpr uint8_t g_tracks = machinedrum::TrackCount;
		constexpr uint8_t g_lastMachine = 191;

		bool valid(const KitChange& _change)
		{
			switch(_change.kind)
			{
			case Kind::Machine:
				return _change.track < g_tracks && _change.value <= g_lastMachine && _change.index < 0x80;
			case Kind::Master:
				return _change.index < MasterEffectCount * MasterEffectParameters && _change.value < 0x80;
			case Kind::Lfo:
			{
				const auto& maxima = LfoSettings::FieldMaximum;
				if(_change.track >= g_tracks || _change.index >= KitChange::LfoFields)
					return false;
				return _change.value <= (_change.index < maxima.size() ? maxima[_change.index] : 0x7f);
			}
			case Kind::Link:
			case Kind::Choke:
				return _change.track < g_tracks && (_change.value < g_tracks || _change.value == MdKit::Off);
			}
			return false;
		}

		struct Writer
		{
			KitChangeMessage message;

			explicit Writer(const uint8_t _command)
			{
				for(const auto byte : {uint8_t{0xf0}, uint8_t{0x00}, uint8_t{0x20}, uint8_t{0x3c},
					codec::product(MachineModel::Machinedrum), uint8_t{0x00}, _command})
					add(byte);
			}
			Writer& add(const uint8_t _byte)
			{
				message.bytes[message.size++] = _byte;
				return *this;
			}
		};

		// What $5B leaves in _track's 24 parameters ($2053e0 to $205662)
		void assign(MdKit& _kit, const uint8_t _track, const uint8_t _id, const uint8_t _scope,
			const std::array<uint8_t, 8>& _defaults)
		{
			auto& values = _kit.parameters[_track];
			if(_id == 113)	// CTR-8P ($20553a)
			{
				// Each knob takes the value of the parameter it aims at, read as the OS reads it, after the knobs
				// before it
				for(size_t i = 0; i < 8; ++i)
					values[i] = _kit.parameters[os163::Ctr8pTargets[2 * i]][os163::Ctr8pTargets[2 * i + 1]];
				for(size_t i = 0; i < 8; ++i)
				{
					// Scope 0 keeps a target in range: a track 0-15, then a parameter 0-23
					auto& effect = values[8 + i];
					if(_scope >= 1 || effect > (i & 1 ? 23 : 15))
						effect = os163::Ctr8pEffects[i];
					if(_scope >= KitChange::AssignScope)
						values[16 + i] = os163::Ctr8pRouting[i];
				}
				return;
			}
			// CTR-RE, -GB, -EQ, -DX: the master effect each controls ($2055e8)
			const bool controller = _id >= 120 && _id < 124;
			const auto* syn = controller
				? &_kit.masterEffects[MasterEffectParameters * os163::CtrBlocks[_id - 120]] : _defaults.data();
			std::copy_n(syn, 8, values.begin());
			const bool mid = _id >= 96 && _id < 112;
			const auto& effects = _id == 112 ? os163::CtrAl : mid ? os163::MidEffects : os163::Effects;
			const auto& routing = _id == 112 ? os163::CtrAl : mid ? os163::MidRouting : os163::Routing;
			if(_scope >= 1)
				std::copy(effects.begin(), effects.end(), values.begin() + 8);
			if(_scope >= KitChange::AssignScope)
				std::copy(routing.begin(), routing.end(), values.begin() + 16);
		}
	}

	std::optional<KitChange> decodeKitChange(const MessageView _message)
	{
		const auto size = _message.size();
		if(size < 10 || size > 12 || _message.back() != 0xf7)
			return std::nullopt;
		const auto command = _message[6];
		if(!codec::hasHeader(MachineModel::Machinedrum, _message, command))
			return std::nullopt;
		KitChange change;
		if(command == g_assignMachine)
		{
			// Track, id, then the UW flag and a scope if the message has them ($2052fe)
			change.kind = Kind::Machine;
			change.track = _message[7];
			change.value = static_cast<uint8_t>(_message[8] | (_message[9] == 1 ? 0x80 : 0));
			change.index = size == 12 ? _message[10] : KitChange::AssignScope;
		}
		else if(size != 10)
		{
			return std::nullopt;
		}
		else if(command >= g_masterEffect && command < g_masterEffect + MasterEffectCount)
		{
			if(_message[7] >= MasterEffectParameters)
				return std::nullopt;
			change.kind = Kind::Master;
			change.index = static_cast<uint8_t>((command - g_masterEffect) * MasterEffectParameters + _message[7]);
			change.value = _message[8];
		}
		else if(command == g_lfoChange)
		{
			change.kind = Kind::Lfo;
			change.track = static_cast<uint8_t>(_message[7] >> 3);
			change.index = static_cast<uint8_t>(_message[7] & 7);
			change.value = _message[8];
		}
		else if(command == g_linkChange || command == g_chokeChange)
		{
			change.kind = command == g_linkChange ? Kind::Link : Kind::Choke;
			change.track = _message[7];
			change.value = _message[8] == g_offOnTheWire ? MdKit::Off : _message[8];
		}
		else
		{
			return std::nullopt;
		}
		if(!valid(change))
			return std::nullopt;
		return change;
	}

	std::optional<KitChangeMessage> encodeKitChange(const KitChange& _change)
	{
		if(!valid(_change))
			return std::nullopt;
		switch(_change.kind)
		{
		case Kind::Machine:
		{
			// Ids 128 and up go out as id - 128 with the UW flag; a scope byte only for a scope but AssignScope
			Writer writer(g_assignMachine);
			writer.add(_change.track).add(static_cast<uint8_t>(_change.value & 0x7f)).add(_change.value >= 128 ? 1 : 0);
			if(_change.index != KitChange::AssignScope)
				writer.add(_change.index);
			return writer.add(0xf7).message;
		}
		case Kind::Master:
			return Writer(static_cast<uint8_t>(g_masterEffect + _change.index / MasterEffectParameters))
				.add(static_cast<uint8_t>(_change.index % MasterEffectParameters)).add(_change.value).add(0xf7).message;
		case Kind::Lfo:
			return Writer(g_lfoChange).add(static_cast<uint8_t>(_change.track << 3 | _change.index)).add(_change.value)
				.add(0xf7).message;
		case Kind::Link:
		case Kind::Choke:
			return Writer(_change.kind == Kind::Link ? g_linkChange : g_chokeChange).add(_change.track)
				.add(_change.value == MdKit::Off ? g_offOnTheWire : _change.value).add(0xf7).message;
		}
		return std::nullopt;
	}

	std::optional<Message> kitChangeMessage(const KitChange& _change)
	{
		const auto encoded = encodeKitChange(_change);
		if(!encoded)
			return std::nullopt;
		return Message(encoded->bytes.begin(), encoded->bytes.begin() + encoded->size);
	}

	bool applyKitChange(const KitChange& _change, MdKit& _kit, const std::array<uint8_t, 8>* _machineDefaults)
	{
		if(!valid(_change))
			return false;
		switch(_change.kind)
		{
		case Kind::Machine:
			if(!_machineDefaults)
				return false;
			// The whole word, as the OS writes it ($205392)
			_kit.machines[_change.track] = _change.value;
			assign(_kit, _change.track, _change.value, _change.index, *_machineDefaults);
			return true;
		case Kind::Master:
			_kit.masterEffects[mdKitMasterIndex(static_cast<uint8_t>(_change.index / MasterEffectParameters),
				static_cast<uint8_t>(_change.index % MasterEffectParameters))] = _change.value;
			return true;
		case Kind::Lfo:
			// The LFO's five settings, then LFOS, LFOD and LFOM, the Track's last three parameters
			if(_change.index < LfoSettings::FieldMaximum.size())
			{
				_kit.lfos[_change.track][_change.index] = _change.value;
				return true;
			}
			_kit.parameters[_change.track][MdKit::ParameterCount - KitChange::LfoFields + _change.index] = _change.value;
			return true;
		case Kind::Link:
			_kit.links[_change.track] = _change.value;
			return true;
		case Kind::Choke:
			_kit.chokes[_change.track] = _change.value;
			return true;
		}
		return false;
	}
}
