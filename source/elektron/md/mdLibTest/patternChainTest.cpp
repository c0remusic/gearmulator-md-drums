// PatternChain's positions and ChainPlayer's MIDI, without firmware: where each pass of a
// chain starts, and what the player forwards for the transport the plug-in sends. What the
// machine does with it is patternChainFirmwareTest's.

#include "mdLib/mdchainplayer.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdpatternchain.h"
#include "mdLib/mdsysexautomation.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Chain = md::PatternChain;
	using Event = synthLib::SMidiEvent;
	constexpr auto Model = md::MachineModel::Machinedrum;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// A01 (16 steps) twice, then A02 (32 steps) once: 2 * 96 + 192 = 384 ticks a round
	Chain makeChain()
	{
		Chain chain;
		require(chain.setEntries({{0, 2}, {1, 1}}), "entries refused");
		require(!chain.isPlayable(), "playable without lengths");
		chain.setLength(0, 16);
		chain.setLength(1, 32);
		require(chain.isPlayable() && chain.roundTicks() == 384, "round of A01 x2, A02 not 384 ticks");
		return chain;
	}

	bool samePass(const std::optional<Chain::Pass>& _pass, const size_t _entry, const uint8_t _number, const uint64_t _start,
		const uint32_t _ticks, const uint8_t _pattern)
	{
		return _pass && _pass->entry == _entry && _pass->pass == _number && _pass->start == _start && _pass->ticks == _ticks
			&& _pass->pattern == _pattern;
	}

	void testChain()
	{
		Chain refused;
		require(!refused.setEntries({{128, 1}}) && !refused.setEntries({{0, 0}})
			&& !refused.setEntries({{0, Chain::MaxPasses + 1}}), "an entry out of range accepted");
		require(!refused.isPlayable() && refused.roundTicks() == 0 && !refused.passAt(0), "an empty chain plays");

		const auto chain = makeChain();
		require(samePass(chain.passAt(0), 0, 0, 0, 96, 0) && samePass(chain.passAt(95), 0, 0, 0, 96, 0), "first pass wrong");
		require(samePass(chain.passAt(96), 0, 1, 96, 96, 0), "second pass of A01 wrong");
		require(samePass(chain.passAt(192), 1, 0, 192, 192, 1) && samePass(chain.passAt(383), 1, 0, 192, 192, 1), "A02 wrong");
		require(samePass(chain.passAt(384), 0, 0, 384, 96, 0), "the chain does not loop");
		require(samePass(chain.passAt(3 * 384 + 100), 0, 1, 3 * 384 + 96, 96, 0), "a later round wrong");

		const auto first = *chain.passAt(0);
		require(chain.lastRequestTick(first) == 88, "Machinedrum's last request tick not 8 ticks before the end");
		Chain monomachine(md::MachineModel::Monomachine);
		require(monomachine.lastRequestTick(first) == 94 && monomachine != Chain(), "Monomachine's last request tick not 2 ticks before the end");
		const auto second = chain.next(first);
		const auto third = chain.next(second);
		const auto fourth = chain.next(third);
		require(samePass(second, 0, 1, 96, 96, 0) && samePass(third, 1, 0, 192, 192, 1)
			&& samePass(fourth, 0, 0, 384, 96, 0), "next passes wrong");

		auto unknown = chain;
		unknown.setLength(1, 0);
		require(!unknown.isPlayable() && !unknown.getLength(1) && unknown.getLength(0) == uint8_t{16}, "a forgotten length still known");
	}

	// Feeds events to a player and keeps what it forwards
	class Feed
	{
	public:
		explicit Feed(md::ChainPlayer& _player) : m_player(_player) {}

		std::vector<Event> send(const uint8_t _a, const uint8_t _b = 0, const uint8_t _c = 0)
		{
			return send(Event(synthLib::MidiEventSource::Internal, _a, _b, _c));
		}

		std::vector<Event> send(const Event& _event)
		{
			std::vector<Event> out;
			m_player.process(_event, out);
			return out;
		}

		// Clocks until the tick _until (excluded); returns the patterns asked for, with their tick
		std::vector<std::pair<uint64_t, uint8_t>> clocks(const uint64_t _from, const uint64_t _until)
		{
			std::vector<std::pair<uint64_t, uint8_t>> asked;
			for(auto tick = _from; tick < _until; ++tick)
			{
				const auto out = send(synthLib::M_TIMINGCLOCK);
				require(!out.empty() && out.front().a == synthLib::M_TIMINGCLOCK, "a clock not forwarded first");
				for(size_t index = 1; index < out.size(); ++index)
				{
					const auto pattern = selected(out[index]);
					require(pattern.has_value(), "a clock forwarded with something else than a pattern selection");
					asked.emplace_back(tick, *pattern);
				}
			}
			return asked;
		}

		static std::optional<uint8_t> selected(const Event& _event)
		{
			if(_event.sysex.empty())
				return std::nullopt;
			const auto status = md::automation::sysex::parseSetStatus(Model, _event.sysex);
			if(!status || status->parameter != md::automation::sysex::StatusParameter::Pattern)
				return std::nullopt;
			return status->value;
		}

	private:
		md::ChainPlayer& m_player;
	};

	void testPlayer()
	{
		using Asked = std::vector<std::pair<uint64_t, uint8_t>>;
		md::ChainPlayer player(Model);
		Feed feed(player);

		// Without a chain, everything passes unchanged
		auto out = feed.send(synthLib::M_SONGPOSITION, 40, 0);
		require(out.size() == 1 && out.front().b == 40, "song position changed without a chain");
		require(feed.send(synthLib::M_CONTINUE).size() == 1 && feed.clocks(240, 250).empty(), "clocks changed without a chain");
		require(feed.send(synthLib::M_STOP).size() == 1, "STOP changed");

		player.setChain(std::make_shared<Chain>(makeChain()));

		// START: A01 selected first, as the machine's pattern is not known
		out = feed.send(synthLib::M_START);
		require(out.size() == 2 && Feed::selected(out[0]) == uint8_t{0} && out[1].a == synthLib::M_START,
			"START without A01 selected before it");
		// A02 asked for on the first clock of A01's second pass, A01 on the first of A02, none on A01's first
		auto asked = feed.clocks(0, 300);
		require(asked == Asked{{96, 1}, {192, 0}}, "not asked for on the first clock of a pass before a change");
		require(player.getPlaying() && player.getPlaying()->entry == 1 && player.getPlaying()->pass == 0,
			"playing pass not published");

		// STOP after A01 was asked for and before it plays: START plays the asked one, nothing to select
		feed.send(synthLib::M_STOP);
		require(!player.getPlaying() && player.getStartPattern() == uint8_t{0}, "STOP did not keep the asked pattern");
		out = feed.send(synthLib::M_START);
		require(out.size() == 1 && out.front().a == synthLib::M_START, "A01 selected again although START plays it");
		asked = feed.clocks(0, 400);
		require(asked == Asked{{96, 1}, {192, 0}}, "a second start did not ask as the first");

		// A01 took over at 384 as asked: STOP then START plays it, nothing to select
		feed.send(synthLib::M_STOP);
		require(player.getStartPattern() == uint8_t{0}, "the pattern playing at STOP not kept");
		out = feed.send(synthLib::M_START);
		require(out.size() == 1, "A01 selected again although the machine plays it");
		feed.send(synthLib::M_STOP);

		// SONG POSITION 56: tick 336, A02's pass from 192: A02 selected, the position moved 24 steps into it,
		// A01 asked for on the first clock
		out = feed.send(synthLib::M_SONGPOSITION, 56, 0);
		require(out.size() == 2 && Feed::selected(out[0]) == uint8_t{1} && out[1].a == synthLib::M_SONGPOSITION
			&& out[1].b == 24 && out[1].c == 0, "song position not moved into A02");
		require(feed.send(synthLib::M_CONTINUE).size() == 1, "CONTINUE changed");
		asked = feed.clocks(336, 340);
		require(asked == Asked{{336, 0}}, "A01 not asked for on the first clock after CONTINUE");
		feed.send(synthLib::M_STOP);

		// Started past the commit (tick 380 of A02's pass ending at 384): nothing asked for in that pass
		out = feed.send(synthLib::M_SONGPOSITION, 63, 0);
		feed.send(synthLib::M_CONTINUE);
		asked = feed.clocks(378, 384);
		require(asked.empty(), "asked for after the commit");
		feed.send(synthLib::M_STOP);

		// Selected elsewhere while stopped: START selects the chain's pattern again
		Event elsewhere(synthLib::MidiEventSource::Host);
		const auto body = md::midiProtocol::selectPattern(Model, 5);
		elsewhere.sysex.push_back(0xf0);
		elsewhere.sysex.insert(elsewhere.sysex.end(), body.begin(), body.end());
		elsewhere.sysex.push_back(0xf7);
		require(feed.send(elsewhere).size() == 1 && player.getStartPattern() == uint8_t{5}, "a selection passing through not seen");
		out = feed.send(synthLib::M_START);
		require(out.size() == 2 && Feed::selected(out[0]) == uint8_t{0}, "the chain's pattern not selected after another was");
		feed.clocks(0, 10);
		feed.send(synthLib::M_STOP);

		// The machine's status, while stopped with nothing asked for, tells what START plays
		Event status(synthLib::MidiEventSource::Device);
		for(const uint8_t byte : {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 0x01, 0xf7})
			status.sysex.push_back(byte);
		player.observe(status);
		require(player.getStartPattern() == uint8_t{1}, "status not taken while stopped");
		out = feed.send(synthLib::M_START);
		require(out.size() == 2 && Feed::selected(out[0]) == uint8_t{0}, "A01 not selected over the status's A02");
		feed.send(synthLib::M_STOP);

		// A Program Change passing through: the pattern START plays is unknown
		feed.send(synthLib::M_PROGRAMCHANGE, 3);
		require(!player.getStartPattern(), "a Program Change kept the start pattern known");

		// No chain again: nothing changed
		player.setChain(nullptr);
		out = feed.send(synthLib::M_START);
		require(out.size() == 1 && out.front().a == synthLib::M_START && feed.clocks(0, 200).empty(),
			"a removed chain still plays");
	}
}

int main()
{
	try
	{
		testChain();
		testPlayer();
		std::printf("patternChainTest: PASS\n");
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("patternChainTest: FAIL %s\n", _error.what());
		return 1;
	}
}
