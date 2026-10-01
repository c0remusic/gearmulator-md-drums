// ChainControl, without firmware: when the project's chain goes to the player and when it is taken
// back, the lengths it waits for, and the chunk the project saves.

#include "mdChainControl.h"

#include "baseLib/binarystream.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using mdJucePlugin::ChainControl;
	using State = ChainControl::State;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void testStates()
	{
		ChainControl control(md::MachineModel::Machinedrum);
		require(control.getState() == State::Off && !control.getPlayer()->getChain(), "a new chain plays");
		require(!control.setEntries({{128, 1}}) && control.getEntries().empty(), "an entry out of range accepted");

		auto revision = control.getRevision();
		require(control.setEntries({{0, 2}, {1, 1}}) && control.getRevision() != revision, "entries not taken");
		require(control.update(true) == State::Off && !control.getPlayer()->getChain(), "a chain turned off plays");
		control.setEnabled(true);
		require(control.update(true) == State::ReadingLengths && control.getMissingLength() == uint8_t{0},
			"a chain without lengths not waiting for A01's");

		revision = control.getRevision();
		control.setLength(0, 16);
		require(control.getMissingLength() == uint8_t{1} && control.update(true) == State::ReadingLengths
			&& control.getRevision() != revision, "A01's length not taken");
		control.setLength(1, 32);
		require(!control.getMissingLength() && control.update(false) == State::NotFollowing
			&& !control.getPlayer()->getChain(), "a chain played while the machine does not follow the host");

		require(control.update(true) == State::Playing, "a complete chain not playing while following");
		const auto handed = control.getPlayer()->getChain();
		require(handed && handed->roundTicks() == 384 && handed->getEntries() == control.getEntries(), "the player got another chain");
		require(control.update(true) == State::Playing && control.getPlayer()->getChain() == handed, "the same chain handed again");

		// An edit while playing reaches the player at once
		require(control.setEntries({{1, 1}, {0, 2}}) && control.getPlayer()->getChain() != handed
			&& control.getPlayer()->getChain()->getEntries().front().pattern == 1, "an edit did not reach the player");
		// A length changed by a later dump reaches it at the next update
		control.setLength(0, 32);
		require(control.update(true) == State::Playing && control.getPlayer()->getChain()->roundTicks() == 3 * 192,
			"a new length did not reach the player");

		require(control.update(false) == State::NotFollowing && !control.getPlayer()->getChain(), "not taken back without the host");
		control.update(true);
		control.setEnabled(false);
		require(control.getState() == State::Off && !control.getPlayer()->getChain(), "not taken back when turned off");
		require(control.setEntries({}) && control.update(true) == State::Off, "an empty chain turned off not off");
		control.setEnabled(true);
		require(control.update(true) == State::Empty, "an empty chain not said empty");
	}

	void testChunk()
	{
		ChainControl saved(md::MachineModel::Machinedrum);
		require(saved.setEntries({{3, 4}, {17, 1}}), "entries refused");
		saved.setEnabled(true);
		saved.setLength(3, 16);
		saved.setLength(17, 64);
		baseLib::BinaryStream stream;
		saved.save(stream);
		std::vector<uint8_t> data;
		stream.toVector(data);
		require(data == std::vector<uint8_t>{1, 2, 3, 4, 16, 17, 1, 64}, "chunk not enabled, count, then pattern, passes and length");

		ChainControl loaded(md::MachineModel::Machinedrum);
		baseLib::BinaryStream input(data);
		require(loaded.load(input) && loaded.isEnabled() && loaded.getEntries() == saved.getEntries()
			&& loaded.getLength(3) == uint8_t{16} && loaded.getLength(17) == uint8_t{64}, "chunk not read back");
		require(loaded.update(true) == State::Playing, "a loaded chain with its lengths not playing");

		ChainControl refused(md::MachineModel::Machinedrum);
		baseLib::BinaryStream bad(std::vector<uint8_t>{1, 1, 3, 0, 16});
		require(!refused.load(bad) && refused.getEntries().empty() && !refused.isEnabled(), "a chunk with 0 passes accepted");
	}
}

int main()
{
	try
	{
		testStates();
		testChunk();
		std::printf("mdChainControlTest: PASS\n");
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdChainControlTest: FAIL %s\n", _error.what());
		return 1;
	}
}
