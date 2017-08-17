/**
 * Author: Dominik Egger
 * Copyright: Distributed Organisms B.V. (DoBots)
 * Date: 9 Mar., 2016
 * License: LGPLv3+, Apache, and/or MIT, your choice
 */

#include "drivers/cs_PWM.h"
#include "util/cs_BleError.h"
#include "drivers/cs_Serial.h"
#include "cfg/cs_Strings.h"
#include "protocol/cs_ErrorCodes.h"

#include <nrf.h>
#include <app_util_platform.h>

/**********************************************************************************************************************
 * Pulse Width Modulation class
 *********************************************************************************************************************/

// Timer channel that determines the period is set to the last channel available.
#define PERIOD_CHANNEL_IDX       5
#define PERIOD_SHORT_CLEAR_MASK  NRF_TIMER_SHORT_COMPARE5_CLEAR_MASK
#define PERIOD_SHORT_STOP_MASK   NRF_TIMER_SHORT_COMPARE5_STOP_MASK

// Timer channel to capture the timer counter at the zero crossing
#define CAPTURE_CHANNEL_IDX      4
#define CAPTURE_TASK             NRF_TIMER_TASK_CAPTURE4

//#define PWM_CENTERED

PWM::PWM() :
		_initialized(false)
		{
#if PWM_ENABLE==1

#endif
}

uint32_t PWM::init(pwm_config_t & config) {
//#if PWM_ENABLE==1
	LOGi(FMT_INIT, "PWM");
	_config = config;

	uint32_t err_code;

	// Setup timer
	nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_CLEAR);
	nrf_timer_bit_width_set(CS_PWM_TIMER, NRF_TIMER_BIT_WIDTH_32);
	nrf_timer_frequency_set(CS_PWM_TIMER, CS_PWM_TIMER_FREQ);
	_maxTickVal = nrf_timer_us_to_ticks(_config.period_us, CS_PWM_TIMER_FREQ);
	LOGd("maxTicks=%u", _maxTickVal);
	nrf_timer_cc_write(CS_PWM_TIMER, getTimerChannel(PERIOD_CHANNEL_IDX), _maxTickVal);
	nrf_timer_mode_set(CS_PWM_TIMER, NRF_TIMER_MODE_TIMER);
	nrf_timer_shorts_enable(CS_PWM_TIMER, PERIOD_SHORT_CLEAR_MASK);
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(0));
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(1));
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(2));
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(3));
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(4));
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(5));


	// Init gpiote and ppi for each channel
	for (uint8_t i=0; i<_config.channelCount; ++i) {
		initChannel(i, _config.channels[i]);
	}

//	uint32_t value = _maxTickVal / 100 * 10;
//	LOGd("value=%u", value);
//	for (uint8_t i=0; i<_config.channelCount; ++i) {
//		nrf_timer_cc_write(CS_PWM_TIMER, getTimerChannel(i), value);
//		enablePwm(i);
//	}

	// Enable timer interrupt
	err_code = sd_nvic_SetPriority(CS_PWM_IRQn, CS_PWM_TIMER_PRIORITY);
	APP_ERROR_CHECK(err_code);
	err_code = sd_nvic_EnableIRQ(CS_PWM_IRQn);
	APP_ERROR_CHECK(err_code);

	// Start the timer
	nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_START);

	_zeroCrossingCounter = 0;

    _initialized = true;
    return ERR_SUCCESS;
//#endif

    return ERR_PWM_NOT_ENABLED;
}

uint32_t PWM::initChannel(uint8_t channel, pwm_channel_config_t& config) {
	LOGd("Configure channel %u as pin %u", channel, _config.channels[channel].pin);
//		nrf_gpio_cfg_output(_config.channels[i].pin);

	_values[channel] = 0;

	// Configure gpiote
//	_gpioteInitStates[channel] = config.inverted ? NRF_GPIOTE_INITIAL_VALUE_LOW : NRF_GPIOTE_INITIAL_VALUE_HIGH;
	_gpioteInitStates[channel] = config.inverted ? NRF_GPIOTE_INITIAL_VALUE_HIGH : NRF_GPIOTE_INITIAL_VALUE_LOW;
	_gpioteInitStatesInversed[channel] = config.inverted ? NRF_GPIOTE_INITIAL_VALUE_LOW : NRF_GPIOTE_INITIAL_VALUE_HIGH;
//	nrf_gpiote_task_configure(CS_PWM_GPIOTE_CHANNEL_START + channel, config.pin, NRF_GPIOTE_POLARITY_TOGGLE, _gpioteInitStates[channel]);
//	nrf_gpiote_task_configure(CS_PWM_GPIOTE_CHANNEL_START + channel, config.pin, NRF_GPIOTE_POLARITY_TOGGLE, _gpioteInitStatesInversed[channel]);


	// Cache ppi channels and gpiote tasks
	_ppiChannels[channel*2] = getPpiChannel(CS_PWM_PPI_CHANNEL_START + channel*2);
	_ppiChannels[channel*2 + 1] = getPpiChannel(CS_PWM_PPI_CHANNEL_START + channel*2 + 1);

	// Make the timer compare event trigger the gpiote task
#ifdef PWM_CENTERED
	nrf_ppi_channel_endpoint_setup(
			_ppiChannels[channel*2],
			(uint32_t)nrf_timer_event_address_get(CS_PWM_TIMER, nrf_timer_compare_event_get(channel*2)),
			nrf_gpiote_task_addr_get(getGpioteTaskOut(CS_PWM_GPIOTE_CHANNEL_START + channel))
	);
	nrf_ppi_channel_endpoint_setup(
			_ppiChannels[channel*2 + 1],
			(uint32_t)nrf_timer_event_address_get(CS_PWM_TIMER, nrf_timer_compare_event_get(channel*2+1)),
			nrf_gpiote_task_addr_get(getGpioteTaskOut(CS_PWM_GPIOTE_CHANNEL_START + channel))
	);
#else
	nrf_ppi_channel_endpoint_setup(
			_ppiChannels[channel*2],
			(uint32_t)nrf_timer_event_address_get(CS_PWM_TIMER, nrf_timer_compare_event_get(channel)),
			nrf_gpiote_task_addr_get(getGpioteTaskOut(CS_PWM_GPIOTE_CHANNEL_START + channel))
	);
	nrf_ppi_channel_endpoint_setup(
			_ppiChannels[channel*2 + 1],
			(uint32_t)nrf_timer_event_address_get(CS_PWM_TIMER, nrf_timer_compare_event_get(PERIOD_CHANNEL_IDX)),
			nrf_gpiote_task_addr_get(getGpioteTaskOut(CS_PWM_GPIOTE_CHANNEL_START + channel))
	);
#endif

	// Enable ppi
	nrf_ppi_channel_enable(_ppiChannels[channel*2]);
	nrf_ppi_channel_enable(_ppiChannels[channel*2 + 1]);
	return 0;
}


uint32_t PWM::deinit() {

#if PWM_ENABLE==1

	LOGi("DeInit PWM");

	_initialized = false;
	return ERR_SUCCESS;
#endif

	return ERR_PWM_NOT_ENABLED;
}

void PWM::setValue(uint8_t channel, uint16_t value) {
	if (!_initialized) {
		LOGe(FMT_NOT_INITIALIZED, "PWM");
		return;
	}
	if (value > 100) {
		value = 100;
	}
	LOGd("Set PWM channel %d to %d", channel, value);
	_values[channel] = value;

	switch (value) {
	case 100: {
		disablePwm(channel);
		nrf_gpio_pin_write(_config.channels[channel].pin, _config.channels[channel].inverted ? 0 : 1);
		break;
	}
	case 0: {
		disablePwm(channel);
		nrf_gpio_pin_write(_config.channels[channel].pin, _config.channels[channel].inverted ? 1 : 0);
		break;
	}
	default: {
		_tickValues[channel] = _maxTickVal * value / 100;
		LOGd("ticks=%u", _tickValues[channel]);
		// Make the timer stop on end of period
		nrf_timer_shorts_enable(CS_PWM_TIMER, PERIOD_SHORT_STOP_MASK);
		// Enable interrupt, set the value in there (at the end/start of the period).
		nrf_timer_int_enable(CS_PWM_TIMER, nrf_timer_compare_int_get(PERIOD_CHANNEL_IDX));
	}
	}
}

uint16_t PWM::getValue(uint8_t channel) {
	if (!_initialized) {
		LOGe(FMT_NOT_INITIALIZED, "PWM");
		return 0;
	}

	LOGe("Invalid channel");
	return 0;
}

void PWM::onZeroCrossing() {
	if (!_initialized) {
		LOGe(FMT_NOT_INITIALIZED, "PWM");
		return;
	}

	nrf_timer_task_trigger(CS_PWM_TIMER, CAPTURE_TASK);
	uint32_t ticks = nrf_timer_cc_read(CS_PWM_TIMER, getTimerChannel(CAPTURE_CHANNEL_IDX));

	// Exponential moving average
	uint32_t alpha = 500; // Discount factor
	_zeroCrossingTicksAvg = ((1000-alpha) * _zeroCrossingTicksAvg + alpha * ticks) / 1000;

//	_zeroCrossingCounter = (_zeroCrossingCounter + 1) % 5;
	_zeroCrossingCounter++;

	if (_zeroCrossingCounter % 2 == 0) {
		int64_t maxTickVal = _maxTickVal;
	//	int64_t targetTicks = _maxTickVal / 2;
		int64_t targetTicks = 0;
		int64_t errTicks = targetTicks - _zeroCrossingTicksAvg;
	//	errTicks = (errTicks + maxTickVal/2) % maxTickVal - maxTickVal/2; // Correct for wrap around

		if (errTicks > maxTickVal / 2) {
			errTicks -= maxTickVal;
		}
		else if (errTicks < -maxTickVal / 2) {
			errTicks += maxTickVal;
		}

		int32_t delta = -errTicks / 100; // Gain

		// Limit the output
		if (delta > maxTickVal / 2000) {
			delta = maxTickVal / 2000;
		}
		if (delta < -maxTickVal / 2000) {
			delta = -maxTickVal / 2000;
		}
		uint32_t newMaxTicks = maxTickVal + delta;

		nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_STOP);
		nrf_timer_task_trigger(CS_PWM_TIMER, CAPTURE_TASK);
		ticks = nrf_timer_cc_read(CS_PWM_TIMER, getTimerChannel(CAPTURE_CHANNEL_IDX));
		// Make sure that the new period compare value is not set lower than the current counter value.
		if (newMaxTicks <= ticks) {
			newMaxTicks = ticks+1;
		}
		nrf_timer_cc_write(CS_PWM_TIMER, getTimerChannel(PERIOD_CHANNEL_IDX), newMaxTicks);
		nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_START);

		if (_zeroCrossingCounter % 50 == 0) {
//			write("%u %u\r\n", ticks, _zeroCrossingTicksAvg);
			write("%u  %u  %lli  %u\r\n", ticks, _zeroCrossingTicksAvg, errTicks, newMaxTicks);
//			write("%u %u\r\n", ticks, newMaxTicks);
		}
	}



	return;

	// Start a new period
	// Need to stop the timer, else the gpio state at start is not consistent.
	// I guess this is because the gpiote toggle sometimes happens before, sometimes after the nrf_gpiote_task_force()
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(PERIOD_CHANNEL_IDX));
	nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_STOP);

	for (uint8_t i=0; i<_config.channelCount; ++i) {
		if (_isPwmEnabled[i]) {
#ifdef PWM_CENTERED
			nrf_gpiote_task_force(CS_PWM_GPIOTE_CHANNEL_START + i, _gpioteInitStates[i]);
#else
			nrf_gpiote_task_force(CS_PWM_GPIOTE_CHANNEL_START + i, _gpioteInitStatesInversed[i]);
#endif
		}
	}
	// Set the counter back to 0, and start the timer again.
	nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_CLEAR);
	nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_START);
}


/////////////////////////////////////////
//          Private functions          //
/////////////////////////////////////////

void PWM::enablePwm(uint8_t channel) {
	if (!_isPwmEnabled[channel]) {
#ifdef PWM_CENTERED
		nrf_gpiote_task_configure(CS_PWM_GPIOTE_CHANNEL_START + channel, _config.channels[channel].pin, NRF_GPIOTE_POLARITY_TOGGLE, _gpioteInitStates[channel]);
#else
		nrf_gpiote_task_configure(CS_PWM_GPIOTE_CHANNEL_START + channel, _config.channels[channel].pin, NRF_GPIOTE_POLARITY_TOGGLE, _gpioteInitStatesInversed[channel]);
#endif
		nrf_gpiote_task_enable(CS_PWM_GPIOTE_CHANNEL_START + channel);
		nrf_gpiote_task_force(CS_PWM_GPIOTE_CHANNEL_START + channel, _gpioteInitStates[channel]);
		_isPwmEnabled[channel] = true;
	}
}

void PWM::disablePwm(uint8_t channel) {
	nrf_gpiote_task_disable(CS_PWM_GPIOTE_CHANNEL_START + channel);
	nrf_gpiote_te_default(CS_PWM_GPIOTE_CHANNEL_START + channel);
	_isPwmEnabled[channel] = false;
}

void PWM::_handleInterrupt() {
	// Disable interrupt until next change.
	nrf_timer_int_disable(CS_PWM_TIMER, nrf_timer_compare_int_get(PERIOD_CHANNEL_IDX));

	// At the interrupt (end of period), set all channels that are dimming.
	for (uint8_t i=0; i<_config.channelCount; ++i) {
//		if ((!isPwmEnabled[i]) && (_tickValues[i] != 0) && (_tickValues[i] != _maxTickVal)) {
		if ((_tickValues[i] != 0) && (_tickValues[i] != _maxTickVal)) {
#ifdef PWM_CENTERED
			nrf_timer_cc_write(CS_PWM_TIMER, getTimerChannel(2*i),   _maxTickVal/2 - _tickValues[i]/2);
			nrf_timer_cc_write(CS_PWM_TIMER, getTimerChannel(2*i+1), _maxTickVal/2 + _tickValues[i]/2);
#else
			nrf_timer_cc_write(CS_PWM_TIMER, getTimerChannel(i), _tickValues[i]);
#endif
//			nrf_gpio_pin_write(_config.channels[i].pin, _config.channels[i].inverted ? 0 : 1);
//			nrf_gpio_pin_write(_config.channels[i].pin, _config.channels[i].inverted ? 1 : 0);
			enablePwm(i);
//			nrf_gpiote_task_force(CS_PWM_GPIOTE_CHANNEL_START + i, _gpioteInitStatesInversed[i]);
		}
	}

	// Don't stop timer on end of period anymore, and start the timer again
	nrf_timer_shorts_disable(CS_PWM_TIMER, PERIOD_SHORT_STOP_MASK);
	nrf_timer_task_trigger(CS_PWM_TIMER, NRF_TIMER_TASK_START);
}

nrf_timer_cc_channel_t PWM::getTimerChannel(uint8_t index) {
	assert(index < 6, "invalid timer channel index");
	switch(index) {
	case 0:
		return NRF_TIMER_CC_CHANNEL0;
	case 1:
		return NRF_TIMER_CC_CHANNEL1;
	case 2:
		return NRF_TIMER_CC_CHANNEL2;
	case 3:
		return NRF_TIMER_CC_CHANNEL3;
	case 4:
		return NRF_TIMER_CC_CHANNEL4;
	case 5:
		return NRF_TIMER_CC_CHANNEL5;
	}
	APP_ERROR_CHECK(NRF_ERROR_INVALID_PARAM);
	return NRF_TIMER_CC_CHANNEL0;
}

nrf_gpiote_tasks_t PWM::getGpioteTaskOut(uint8_t index) {
	assert(index < 8, "invalid gpiote task index");
	switch(index) {
	case 0:
		return NRF_GPIOTE_TASKS_OUT_0;
	case 1:
		return NRF_GPIOTE_TASKS_OUT_1;
	case 2:
		return NRF_GPIOTE_TASKS_OUT_2;
	case 3:
		return NRF_GPIOTE_TASKS_OUT_3;
	case 4:
		return NRF_GPIOTE_TASKS_OUT_4;
	case 5:
		return NRF_GPIOTE_TASKS_OUT_5;
	case 6:
		return NRF_GPIOTE_TASKS_OUT_6;
	case 7:
		return NRF_GPIOTE_TASKS_OUT_7;
	}
	APP_ERROR_CHECK(NRF_ERROR_INVALID_PARAM);
	return NRF_GPIOTE_TASKS_OUT_0;
}

nrf_ppi_channel_t PWM::getPpiChannel(uint8_t index) {
	assert(index < 16, "invalid ppi channel index");
	switch(index) {
	case 0:
		return NRF_PPI_CHANNEL0;
	case 1:
		return NRF_PPI_CHANNEL1;
	case 2:
		return NRF_PPI_CHANNEL2;
	case 3:
		return NRF_PPI_CHANNEL3;
	case 4:
		return NRF_PPI_CHANNEL4;
	case 5:
		return NRF_PPI_CHANNEL5;
	case 6:
		return NRF_PPI_CHANNEL6;
	case 7:
		return NRF_PPI_CHANNEL7;
	case 8:
		return NRF_PPI_CHANNEL8;
	case 9:
		return NRF_PPI_CHANNEL9;
	case 10:
		return NRF_PPI_CHANNEL10;
	case 11:
		return NRF_PPI_CHANNEL11;
	case 12:
		return NRF_PPI_CHANNEL12;
	case 13:
		return NRF_PPI_CHANNEL13;
	case 14:
		return NRF_PPI_CHANNEL14;
	case 15:
		return NRF_PPI_CHANNEL15;
	}
	return NRF_PPI_CHANNEL0;
}



// Interrupt handler
extern "C" void CS_PWM_TIMER_IRQ(void) {
//	if (nrf_timer_event_check(CS_PWM_TIMER, nrf_timer_compare_event_get(PERIOD_CHANNEL_IDX))) {
	nrf_timer_event_clear(CS_PWM_TIMER, nrf_timer_compare_event_get(PERIOD_CHANNEL_IDX));
	PWM::getInstance()._handleInterrupt();
//	}
}
