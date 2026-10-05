#include "mdLib/mdsim.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
	void require(const bool _condition, const char* _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	uint32_t configureUart(md::Sim& _sim, const unsigned _uart)
	{
		const auto base = _uart == md::Sim::g_uartPanel ? md::Sim::g_uart2Base : md::Sim::g_uart1Base;
		// Select receive-ready mode and enable RX/TX. This does not validate the
		// incomplete command, mode-pointer, FIFO-full interrupt or serial timing model.
		_sim.write8(base + md::Sim::g_uartMr, 0x13);
		_sim.write8(base + md::Sim::g_uartCr, 0x05);
		const auto icr = _uart == md::Sim::g_uartPanel ? md::Sim::g_icrUart2 : md::Sim::g_icrUart1;
		_sim.write8(icr, 3 << 2);
		_sim.write8(base + md::Sim::g_uartIvr, 0x60);
		_sim.write16(md::Sim::g_imr, 0);
		return base;
	}

	void sourceStatus(const unsigned _uart)
	{
		md::Sim sim;
		const auto base = configureUart(sim, _uart);
		// MCF5206EUM 12.4.1.10/.11: UIMR gates delivery, not UISR source status.
		sim.write8(base + md::Sim::g_uartIsr, 0);
		require(sim.read8(base + md::Sim::g_uartIsr) == md::Sim::g_uimrTxRdy,
			"masked transmitter readiness disappeared from UISR");
		sim.queueRx(_uart, 0x42);
		const auto ready = sim.read8(base + md::Sim::g_uartIsr);
		require(ready == (md::Sim::g_uimrTxRdy | md::Sim::g_uimrRxRdy), "incorrect ready sources in UISR");
		require(!sim.isReceiveInterruptEnabled(_uart), "status read changed UIMR");
		require(sim.queuedRxBytes(_uart) == 1, "status read consumed receive data");
		const auto other = _uart == md::Sim::g_uartMidi ? md::Sim::g_uartPanel : md::Sim::g_uartMidi;
		const auto otherBase = other == md::Sim::g_uartPanel ? md::Sim::g_uart2Base : md::Sim::g_uart1Base;
		for(unsigned mask = 0; mask < 256; ++mask)
		{
			sim.write8(base + md::Sim::g_uartIsr, static_cast<uint8_t>(mask));
			require(sim.read8(base + md::Sim::g_uartIsr) == ready, "UIMR write changed source status");
			require(sim.isReceiveInterruptEnabled(_uart) == ((mask & md::Sim::g_uimrRxRdy) != 0),
				"status read changed UIMR");
			require(!sim.isReceiveInterruptEnabled(other), "mask write affected the other UART");
			require(sim.read8(otherBase + md::Sim::g_uartIsr) == md::Sim::g_uimrTxRdy,
				"receive status leaked to the other UART");
		}
		require(sim.queuedRxBytes(_uart) == 1, "repeated status reads consumed receive data");
		require(sim.isReceiveInterruptEnabled(_uart), "UIMR write did not enable receive interrupts");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x42, "receive data changed");
		require(sim.read8(base + md::Sim::g_uartIsr) == md::Sim::g_uimrTxRdy,
			"drained receiver still reported ready");
		require(sim.isReceiveInterruptEnabled(_uart), "draining receive data changed UIMR");
	}

	void receiveUnmask(const unsigned _uart)
	{
		md::Sim sim;
		const auto base = configureUart(sim, _uart);
		sim.write8(base + md::Sim::g_uartIsr, 0);
		sim.queueRx(_uart, 0x57);
		uint8_t level = 0, vector = 0;
		require(!sim.takeNextInterrupt(level, vector), "masked UART requested service");
		require(!sim.needsInterruptCheck(), "idle interrupt scan did not clear the check gate");
		// No new byte or TX-ready interrupt may be needed to service pending RX.
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(sim.needsInterruptCheck(), "unmasking did not schedule an interrupt check");
		require(sim.takeNextInterrupt(level, vector), "unmasking stranded an already-received byte");
		require(level == 3 && vector == 0x60, "receive request used the wrong level/vector");
		require(sim.queuedRxBytes(_uart) == 1, "interrupt offer consumed receive data");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x57, "unmasking changed pending receive data");
		require(!sim.takeNextInterrupt(level, vector), "drained receiver requested extra service");
	}

	void pendingMaskChanges(const unsigned _uart)
	{
		md::Sim sim;
		const auto base = configureUart(sim, _uart);
		const auto source = _uart == md::Sim::g_uartPanel ? md::Sim::g_irqSrcUart2 : md::Sim::g_irqSrcUart1;
		sim.write16(md::Sim::g_imr, static_cast<uint16_t>(1u << source));
		sim.queueRx(_uart, 0x42);
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		uint8_t level = 0, vector = 0;
		require(!sim.takeNextInterrupt(level, vector), "global mask did not block RX");
		require(!sim.needsInterruptCheck(), "globally masked scan did not settle");
		sim.write16(md::Sim::g_imr, 0);
		require(sim.needsInterruptCheck(), "global unmask did not reactivate the check gate");
		require(sim.takeNextInterrupt(level, vector), "global unmask lost pending RX");
		require(level == 3 && vector == 0x60, "global unmask changed interrupt routing");

		// None of these writes creates new RX data or consumes the offered byte.
		// Bit 7 changes another mask bit without enabling the model's TX source.
		for(const auto mask : {0x02, 0x82, 0x80, 0x82, 0x00, 0x02, 0x02})
		{
			sim.write8(base + md::Sim::g_uartIsr, static_cast<uint8_t>(mask));
			require(!sim.takeNextInterrupt(level, vector), "mask write duplicated an offered RX request");
			require(!sim.needsInterruptCheck(), "mask-only scan did not settle");
		}
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x42, "mask changes lost the offered byte");
		sim.queueRx(_uart, 0x43);
		require(sim.takeNextInterrupt(level, vector), "fresh receive data did not rearm RX");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x43, "fresh receive data changed");
		require(!sim.takeNextInterrupt(level, vector), "freshly drained RX requested extra service");
	}

	void maskedDrain(const unsigned _uart)
	{
		md::Sim sim;
		const auto base = configureUart(sim, _uart);
		sim.queueRx(_uart, 0x41);
		sim.queueRx(_uart, 0x42);
		sim.queueRx(_uart, 0x43);
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x41, "masked polling changed the first byte");
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		uint8_t level = 0, vector = 0;
		require(sim.takeNextInterrupt(level, vector), "partially drained RX was stranded on unmask");
		require(!sim.takeNextInterrupt(level, vector), "queued bytes duplicated the current RX offer");
		sim.write8(base + md::Sim::g_uartIsr, 0);
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x42, "masked drain changed the second byte");
		require(!sim.takeNextInterrupt(level, vector), "draining bypassed the UART mask");
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(sim.takeNextInterrupt(level, vector), "masked drain lost the next byte's RX request");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x43, "masked drain changed the last byte");
		require(!sim.takeNextInterrupt(level, vector), "fully drained RX requested extra service");

		// Draining before unmask must not resurrect a stale receive request.
		sim.write8(base + md::Sim::g_uartIsr, 0);
		sim.queueRx(_uart, 0x44);
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x44, "masked polling changed fresh data");
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(!sim.takeNextInterrupt(level, vector), "unmask resurrected drained RX");
		require(!sim.needsInterruptCheck(), "drained RX left the interrupt-check gate hot");

		sim.queueRx(_uart, 0x45);
		sim.reset();
		configureUart(sim, _uart);
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(sim.queuedRxBytes(_uart) == 0, "reset retained receive data");
		require(!sim.takeNextInterrupt(level, vector), "reset retained a receive request");
	}

	// The transmitter as MD OS 1.63 programs it: 8N1 on the internal baud generator. UBG=8 on the panel
	// UART, 2560 cycles a byte; UBG=40 on the MIDI UART, 12800 cycles a byte (31250 baud at 40 MHz).
	struct TransmitConfig
	{
		unsigned uart;
		uint32_t base;
		uint8_t divisor;
		uint32_t cycles;
		uint32_t icr;
		const char* name;
	};
	constexpr TransmitConfig g_panelTransmit{md::Sim::g_uartPanel, md::Sim::g_uart2Base, 0x08, 2560, md::Sim::g_icrUart2, "panel"};
	constexpr TransmitConfig g_midiTransmit{md::Sim::g_uartMidi, md::Sim::g_uart1Base, 0x28, 12800, md::Sim::g_icrUart1, "MIDI"};

	void configureTransmitter(md::Sim& _sim, const TransmitConfig& _config)
	{
		_sim.write8(_config.base + md::Sim::g_uartMr, 0xb3);
		_sim.write8(_config.base + md::Sim::g_uartMr, 0x07);
		_sim.write8(_config.base + md::Sim::g_uartUsr, 0xdd);
		_sim.write8(_config.base + md::Sim::g_uartBg1, 0x00);
		_sim.write8(_config.base + md::Sim::g_uartBg2, _config.divisor);
		_sim.write8(_config.base + md::Sim::g_uartCr, 0x04);
	}

	void transmitPacing(const TransmitConfig& _config)
	{
		md::Sim sim;
		const auto base = _config.base;
		const auto cycles = _config.cycles;
		std::array<uint8_t, 3> received{};
		size_t count = 0;
		sim.setTransmitCallback(_config.uart, [&](const uint8_t byte)
		{
			if(count < received.size())
				received[count++] = byte;
		});
		// The other UART untouched transmits at once and adds no deadline
		const auto other = _config.uart == md::Sim::g_uartMidi ? md::Sim::g_uartPanel : md::Sim::g_uartMidi;
		unsigned otherReceived = 0;
		sim.setTransmitCallback(other, [&](uint8_t) { ++otherReceived; });

		configureTransmitter(sim, _config);
		require(sim.read8(base + md::Sim::g_uartUsr)
			== (md::Sim::g_usrTxEmp | md::Sim::g_usrTxRdy),
			"enabled empty transmitter did not report ready");

		sim.write8(base + md::Sim::g_uartRxTx, 0x11);
		require(count == 0 && sim.cyclesUntilNextUartTransmit() == cycles,
			"first byte bypassed its configured wire time");
		require(sim.read8(base + md::Sim::g_uartUsr) == md::Sim::g_usrTxRdy,
			"shift-register load did not free the holding register");
		sim.write8(base + md::Sim::g_uartRxTx, 0x22);
		sim.write8(base + md::Sim::g_uartRxTx, 0x33);
		require(sim.read8(base + md::Sim::g_uartUsr) == 0,
			"full holding register still reported ready");
		sim.write8((other == md::Sim::g_uartPanel ? md::Sim::g_uart2Base : md::Sim::g_uart1Base) + md::Sim::g_uartRxTx, 0x44);
		require(otherReceived == 1 && sim.cyclesUntilNextUartTransmit() == cycles,
			"an unconfigured UART did not transmit at once");

		sim.exec(cycles - 1);
		require(count == 0 && sim.cyclesUntilNextUartTransmit() == 1,
			"byte completed before its stop bit");
		sim.exec(1);
		require(count == 1 && received[0] == 0x11
			&& sim.cyclesUntilNextUartTransmit() == cycles,
			"first completion did not advance the queued holding byte");
		require(sim.read8(base + md::Sim::g_uartUsr) == md::Sim::g_usrTxRdy,
			"holding-to-shift transfer did not reassert ready");
		sim.exec(cycles);
		require(count == 2 && received[1] == 0x22,
			"second byte did not complete at its wire deadline");
		require(sim.read8(base + md::Sim::g_uartUsr)
			== (md::Sim::g_usrTxEmp | md::Sim::g_usrTxRdy),
			"drained transmitter did not report empty");
		require(sim.cyclesUntilNextUartTransmit() == md::Sim::g_noTimerInterruptDeadline,
			"drained transmitter retained a deadline");
	}

	void transmitInterruptTiming(const TransmitConfig& _config)
	{
		md::Sim sim;
		const auto base = _config.base;
		unsigned received = 0;
		sim.setTransmitCallback(_config.uart, [&](uint8_t) { ++received; });
		configureTransmitter(sim, _config);
		sim.write8(_config.icr, 3 << 2);
		sim.write8(base + md::Sim::g_uartIvr, 0x60);
		sim.write16(md::Sim::g_imr, 0);
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrTxRdy);

		uint8_t level = 0, vector = 0;
		require(sim.takeNextInterrupt(level, vector) && level == 3 && vector == 0x60,
			"enabling an empty transmitter did not request its first byte");
		sim.write8(base + md::Sim::g_uartRxTx, 0x11);
		require(sim.takeNextInterrupt(level, vector),
			"loading the shift register did not request a second holding byte");
		sim.write8(base + md::Sim::g_uartRxTx, 0x22);
		require(!sim.takeNextInterrupt(level, vector),
			"full holding register requested another transmit byte");
		sim.exec(_config.cycles - 1);
		require(received == 0 && !sim.takeNextInterrupt(level, vector),
			"transmit interrupt arrived before the first stop bit completed");
		sim.exec(1);
		require(received == 1 && sim.takeNextInterrupt(level, vector),
			"character completion did not reassert TxRDY interrupt");
		require(!sim.takeNextInterrupt(level, vector),
			"one holding-register transition offered duplicate interrupts");
	}
}

int main()
{
	unsigned failures = 0;
	for(unsigned uart = 0; uart < md::Sim::g_uartCount; ++uart)
	{
		// Run independently so a status failure cannot hide the unmasking regression.
		for(const auto test : {sourceStatus, receiveUnmask, pendingMaskChanges, maskedDrain})
		{
			try
			{
				test(uart);
			}
			catch(const std::exception& error)
			{
				std::cerr << "UART " << uart << ": " << error.what() << '\n';
				++failures;
			}
		}
	}
	for(const auto& config : {g_panelTransmit, g_midiTransmit})
	{
		try
		{
			transmitPacing(config);
			transmitInterruptTiming(config);
		}
		catch(const std::exception& error)
		{
			std::cerr << config.name << " transmit pacing: " << error.what() << '\n';
			++failures;
		}
	}
	if(failures)
		return 1;
	std::cout << "UART register, receive-unmask, and panel and MIDI transmit timing tests passed\n";
	return 0;
}
