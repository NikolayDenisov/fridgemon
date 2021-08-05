#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "radio_config.h"
#include "nrf_gpio.h"
#include "app_timer.h"
#include "boards.h"
#include "bsp.h"
#include "nordic_common.h"
#include "nrf_error.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_temp.h"
#include "nrf_nvmc.h"
#include "nrf_calendar.h"

#define FLASHWRITE_EXAMPLE_MAX_STRING_LEN       (62u)
#define FLASHWRITE_EXAMPLE_BLOCK_VALID          (0xA55A5AA5)
#define FLASHWRITE_EXAMPLE_BLOCK_INVALID        (0xA55A0000)
#define FLASHWRITE_EXAMPLE_BLOCK_NOT_INIT       (0xFFFFFFFF)


static bool run_time_updates = false;

typedef struct {
	uint32_t magic_number;
	uint32_t buffer[FLASHWRITE_EXAMPLE_MAX_STRING_LEN + 1];
} flashwrite_example_flash_data_t;

typedef struct {
	uint32_t  addr;
	uint32_t  pg_size;
	uint32_t  pg_num;
	flashwrite_example_flash_data_t *m_p_flash_data;
} flashwrite_example_data_t;

static flashwrite_example_data_t m_data;

static uint32_t                   packet;                    /**< Packet to transmit. */

static void timestamp()
{
    time_t ltime; /* calendar time */
    ltime=time(NULL); /* get current cal time */
    NRF_LOG_INFO("timestamp = %s", asctime( localtime(&ltime) ) );
    NRF_LOG_INFO("Calibrated time:\t%s\r\n", nrf_cal_get_time_string(true));
}

/**@brief Function for sending packet.
*/
void send_packet()
{
	// send the packet:
	NRF_RADIO->EVENTS_READY = 0U;
	NRF_RADIO->TASKS_TXEN   = 1;

	while (NRF_RADIO->EVENTS_READY == 0U)
	{
		// wait
	}
	NRF_RADIO->EVENTS_END  = 0U;
	NRF_RADIO->TASKS_START = 1U;

	while (NRF_RADIO->EVENTS_END == 0U)
	{
		// wait
	}

	uint32_t err_code = bsp_indication_set(BSP_INDICATE_SENT_OK);
	//NRF_LOG_INFO("The packet was sent");
	APP_ERROR_CHECK(err_code);

	NRF_RADIO->EVENTS_DISABLED = 0U;
	// Disable radio
	NRF_RADIO->TASKS_DISABLE = 1U;

	while (NRF_RADIO->EVENTS_DISABLED == 0U)
	{
		// wait
	}
}


/**@brief Function for initialization oscillators.
*/
void clock_initialization()
{
	/* Start 16 MHz crystal oscillator */
	NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
	NRF_CLOCK->TASKS_HFCLKSTART    = 1;

	/* Wait for the external oscillator to start up */
	while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0)
	{
		// Do nothing.
	}

	/* Start low frequency crystal oscillator for app_timer(used by bsp)*/
	NRF_CLOCK->LFCLKSRC            = (CLOCK_LFCLKSRC_SRC_Xtal << CLOCK_LFCLKSRC_SRC_Pos);
	NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
	NRF_CLOCK->TASKS_LFCLKSTART    = 1;

	while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0)
	{
		// Do nothing.
	}
}

void read_temp() {
	uint32_t temp;
	nrf_temp_init();
	//Start temperature measurement
	NRF_TEMP->TASKS_START = 1;
	while(NRF_TEMP->EVENTS_DATARDY == 0) {
		//Temperature measurement complete, data ready
	}
	NRF_TEMP->EVENTS_DATARDY = 0;
	temp = nrf_temp_read()/4;
	//Stop temperature measurement
	NRF_TEMP->TASKS_STOP = 1;
	NRF_LOG_INFO("Actual temperature: %d", (int)temp);
        flashwrite_write(temp);
}


static void flash_page_init(void) {
	m_data.pg_num = NRF_FICR->CODESIZE - 1;
	m_data.pg_size = NRF_FICR->CODEPAGESIZE;
	m_data.addr = (m_data.pg_num * m_data.pg_size);
	m_data.m_p_flash_data = (flashwrite_example_flash_data_t *)m_data.addr;
	while (1)
	{
		if (m_data.m_p_flash_data->magic_number == FLASHWRITE_EXAMPLE_BLOCK_VALID) {
			return;
		}

		if (m_data.m_p_flash_data->magic_number == FLASHWRITE_EXAMPLE_BLOCK_INVALID) {
			++m_data.m_p_flash_data;
			continue;
		}
		nrf_nvmc_page_erase(m_data.addr);
		return;
	}
}

static void flash_string_write(uint32_t address, const char *src, uint32_t num_words){
	uint32_t i;
	NRF_LOG_INFO("num_words = %d", num_words);
        NRF_LOG_INFO("flash_string_write src = %s", src);
	// Enable write.
	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
	while(NRF_NVMC->READY == NVMC_READY_READY_Busy) {
	}
	for(i = 0; i < num_words; i++) {
		/* Only full 32-bit words can be written to Flash. */
		((uint32_t*)address)[i] = 0x000000FFUL & (uint32_t)((uint8_t)src[i]);
		while(NRF_NVMC->READY == NVMC_READY_READY_Busy) {
		}
	}
	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
	while(NRF_NVMC->READY == NVMC_READY_READY_Busy){
	}
}

static void flashwrite_erase() {
	nrf_nvmc_page_erase(m_data.addr);
	m_data.m_p_flash_data = (flashwrite_example_data_t *)m_data.addr;
}

static void flashwrite_read() 
{
    flashwrite_example_flash_data_t * p_data = (flashwrite_example_flash_data_t *)m_data.addr;
    char string_buff[FLASHWRITE_EXAMPLE_MAX_STRING_LEN + 1]; // + 1 for end of string

    if ((p_data == m_data.m_p_flash_data) && (p_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_VALID))
    {
        NRF_LOG_INFO("Please write something first.\r\n");
        return;
    }

    while (p_data <= m_data.m_p_flash_data)
    {
        if ((p_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_VALID) &&
            (p_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_INVALID))
        {
            NRF_LOG_INFO("Corrupted data found.\r\n");
            return;
        }
        uint8_t i;
        for (i = 0 ; i <= FLASHWRITE_EXAMPLE_MAX_STRING_LEN; i++)
        {
            string_buff[i] = (char)p_data->buffer[i];
        }

        NRF_LOG_DEBUG("Flash data = %s\r\n", (uint32_t)string_buff);
        ++p_data;
    }
}

void flashwrite_write(uint32_t input) {
	static uint16_t const page_size = 4096;
        char temp_s[3];
        char final_temp[1][3];
	NRF_LOG_INFO("flashwrite_write input = %d", input);
	sprintf(temp_s, "%d", input);
        strcpy(final_temp[0], temp_s);
	NRF_LOG_INFO("Temp_s = %c", *final_temp);
        printf("\nYou have entered: %c", final_temp[0]);
	uint32_t len;
	len = strlen(final_temp[0]);
	NRF_LOG_INFO("len = %d.", len);

	if (len > FLASHWRITE_EXAMPLE_MAX_STRING_LEN) {
		NRF_LOG_INFO("Too long string. Please limit entered string to %d chars.\r\n", FLASHWRITE_EXAMPLE_MAX_STRING_LEN);
		return;
	}
	if((m_data.m_p_flash_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_NOT_INIT) &&
			(m_data.m_p_flash_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_VALID)) {
		NRF_LOG_INFO("Flash corrupted, please errase it first.");
		return;
	}
	if (m_data.m_p_flash_data->magic_number == FLASHWRITE_EXAMPLE_BLOCK_VALID) {
		uint32_t new_end_addr = (uint32_t)(m_data.m_p_flash_data + 2);
		uint32_t diff = new_end_addr - m_data.addr;
		if (diff > page_size) {
			NRF_LOG_INFO("Not enough space - please erase flash first.\r\n");
			return;
		}
		nrf_nvmc_write_word((uint32_t)&m_data.m_p_flash_data->magic_number, FLASHWRITE_EXAMPLE_BLOCK_INVALID);
		++m_data.m_p_flash_data;

	}
	//++len -> store also end of string '\0'
	flash_string_write((uint32_t)&m_data.m_p_flash_data->buffer, final_temp[0], ++len);
	nrf_nvmc_write_word((uint32_t)&m_data.m_p_flash_data->magic_number, FLASHWRITE_EXAMPLE_BLOCK_VALID);
}

/**@brief Function for handling bsp events.
*/
void bsp_evt_handler(bsp_event_t evt)
{
	uint32_t prep_packet = 0;
	switch (evt)
	{
		case BSP_EVENT_KEY_0:
			/* Fall through. */
		case BSP_EVENT_KEY_1:
			/* Fall through. */
		case BSP_EVENT_KEY_2:
			/* Fall through. */
		case BSP_EVENT_KEY_3:
			/* Fall through. */
		case BSP_EVENT_KEY_4:
			/* Fall through. */
		case BSP_EVENT_KEY_5:
			/* Fall through. */
		case BSP_EVENT_KEY_6:
			/* Fall through. */
		case BSP_EVENT_KEY_7:
			/* Get actual button state. */
			for (int i = 0; i < BUTTONS_NUMBER; i++)
			{
				prep_packet |= (bsp_board_button_state_get(i) ? (1 << i) : 0);
			}
			break;
		default:
			/* No implementation needed. */
			break;
	}
	if(prep_packet == 4) {
		NRF_LOG_INFO("Start erase memory");
		flashwrite_erase();
                timestamp();
	}else if (prep_packet == 8) {
		NRF_LOG_INFO("Print flash");
		flashwrite_read();
	}
        else {
          NRF_LOG_INFO("Write data in flash");
          read_temp();
        }
	packet = prep_packet;
}

/**@brief Function for initializing the nrf log module. */
static void log_init(void) {
	ret_code_t err_code = NRF_LOG_INIT(NULL);
	APP_ERROR_CHECK(err_code);
	NRF_LOG_DEFAULT_BACKENDS_INIT();
}

void print_current_time()
{
    NRF_LOG_INFO("Uncalibrated time:\t%s\r\n", nrf_cal_get_time_string(false));
    NRF_LOG_INFO("Calibrated time:\t%s\r\n", nrf_cal_get_time_string(true));
}

void calendar_updated()
{
    if(run_time_updates)
    {
        print_current_time();
    }
}

/**
 * @brief Function for application main entry.
 * @return 0. int return type required by ANSI/ISO standard.
 */
int main(void)
{
	uint32_t err_code = NRF_SUCCESS;

	clock_initialization();

	err_code = app_timer_init();
	APP_ERROR_CHECK(err_code);

	log_init();

	err_code = bsp_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS, bsp_evt_handler);
	APP_ERROR_CHECK(err_code);

	// Set radio configuration parameters
	radio_configure();

	// Set payload pointer
	NRF_RADIO->PACKETPTR = (uint32_t)&packet;

	err_code = bsp_indication_set(BSP_INDICATE_USER_STATE_OFF);
	NRF_LOG_INFO("Radio transmitter example started.");
	NRF_LOG_INFO("Press Any Button");
	APP_ERROR_CHECK(err_code);

	flash_page_init();
	flashwrite_erase();

        nrf_cal_init();
        nrf_cal_set_callback(calendar_updated, 4);

        print_current_time();


	while (true)
	{
		if (packet != 0)
		{
			send_packet();
			//NRF_LOG_INFO("The contents of the package was %u", (unsigned int)packet);
			packet = 0;
		}
		NRF_LOG_FLUSH();
		__WFE();
	}
}


/**
 *@}
 **/
