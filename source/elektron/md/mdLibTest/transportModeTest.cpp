#include "mdLib/mdhardware.h"

#include <iostream>
#include <stdexcept>

namespace
{
	void require(const bool condition, const char* message)
	{
		if(!condition)
			throw std::runtime_error(message);
	}

	void testKnownModes()
	{
		using md::TransportMode;
		require(md::parseTransportMode("serial") == TransportMode::Serial, "serial not recognized");
		require(md::parseTransportMode("parallel") == TransportMode::Parallel, "parallel not recognized");
		require(md::parseTransportMode("pair") == TransportMode::Pair, "pair not recognized");
	}

	void testUnsetValues()
	{
		// The caller keeps its default for all of these: the model's mode in the
		// device, Serial in a bare Hardware. The unknown ones print a warning.
		require(!md::parseTransportMode(nullptr), "an unset MDMM_TRANSPORT selected a mode");
		require(!md::parseTransportMode(""), "an empty MDMM_TRANSPORT selected a mode");
		require(!md::parseTransportMode("pairs"), "a misspelt MDMM_TRANSPORT selected a mode");
		require(!md::parseTransportMode("Pair"), "MDMM_TRANSPORT modes are lower case");
		require(!md::parseTransportMode(" pair"), "MDMM_TRANSPORT is not trimmed");
	}
}

int main()
{
	try
	{
		testKnownModes();
		testUnsetValues();
		std::cout << "MDMM_TRANSPORT parsing passed\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
