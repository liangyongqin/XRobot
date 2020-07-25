/* 
	运行命令行交互界面（Command Line Interface）。
*/

/* Includes ------------------------------------------------------------------*/
#include "task\user_task.h"

#include <stdio.h>
#include <stdbool.h>

#include "bsp\usb.h"
#include "bsp\flash.h"

#include "FreeRTOS.h"
#include "task.h"
#include "component\FreeRTOS_CLI.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define MAX_INPUT_LENGTH	64

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
Task_Param_t task_param; //TODO: Add static when release

static const char* const CLI_WELCOME_MESSAGE = 
	"\r\n"
	"  ______         __           _______               __              \r\n"
	" |   __ \\.-----.|  |--.-----.|   |   |.---.-.-----.|  |_.-----.----.\r\n"
	" |      <|  _  ||  _  |  _  ||       ||  _  |__ --||   _|  -__|   _|\r\n"
	" |___|__||_____||_____|_____||__|_|__||___._|_____||____|_____|__|  \r\n"
	"           Q I N G D A O  U N I V E R S I T Y    2 0 2 0            \r\n"
	" -------------------------------------------------------------------\r\n"
	" Firmware Version: 0.0.1                                            \r\n"
	" -------------------------------------------------------------------\r\n"
	" FreeRTOS CLI. Type 'help' to view a list of registered commands.   \r\n"
	"\r\n";

static BaseType_t EndianCommand(char *out_buffer, size_t len, const char *command_string) {
	(void)command_string;
	
	if (out_buffer == NULL)
		return pdFALSE;
	
	uint8_t list[2] = {0x11, 0x22};
	uint16_t force_convert = ((uint16_t*)list)[0];
	uint16_t assembled = (uint16_t)(list[0] | (list[1] << 8));
	
	len -= 1;
	static uint8_t stage = 0;
	switch (stage) {
		case 0:
			snprintf(out_buffer, len, "a[2] = {0x11, 0x22}\r\n");
			stage ++;
			return pdPASS;
		case 1:
			snprintf(out_buffer, len, "Force convert to uint16 list, we got: 0x%x\r\n", force_convert);
			stage ++;
			return pdPASS;
		case 2:
			snprintf(out_buffer, len, "Manually assemble a[1], a[0], we got: 0x%x\r\n", assembled);
			stage ++;
			return pdPASS;
		case 3:
			if (force_convert == assembled)
				snprintf(out_buffer, len, "Small endian\r\n");
			else
				snprintf(out_buffer, len, "Big endian\r\n");
			stage ++;
			return pdPASS;
		default:
			snprintf(out_buffer, len, "\r\n");
			stage = 0;
			return pdFALSE;
	}
}

static const CLI_Command_Definition_t endian = {
	"endian",
	"\r\nendian:\r\n Endian experiment.\r\n\r\n",
	EndianCommand,
	0, 
};

static BaseType_t StatsCommand(char *out_buffer, size_t len, const char *command_string) {
	static const char *const task_list_header = 
		"\r\n"
		"Task list\r\n"
		"Task          State  Priority  Stack	#\r\n"
		"************************************************\r\n";
	
	static const char *const run_time_header = 
		"\r\n"
		"Run time stats\r\n"
		"Task            Abs Time      % Time\r\n"
		"****************************************\r\n";

	static const char *const heap_header = 
		"\r\n"
		"Heap stats\r\n"
		"total(B)	free(B)	used(B)\r\n"
		"*******************************\r\n";
	
	(void)command_string;
	if (out_buffer == NULL)
		return pdFALSE;
	
	HeapStats_t heap_stats;
	
	len -= 1;
	static uint8_t stage = 0;
	switch (stage) {
		case 0:
			strncpy(out_buffer, task_list_header, len);
			stage ++;
			return pdPASS;
		case 1:
			vTaskList(out_buffer);
			stage ++;
			return pdPASS;
		case 2:
			strncpy(out_buffer, run_time_header, len);
			stage ++;
			return pdPASS;
		case 3:
			vTaskGetRunTimeStats(out_buffer);
			stage ++;
			return pdPASS;
		case 4:
			strncpy(out_buffer, heap_header, len);
			stage ++;
			return pdPASS;
		case 5:	
			vPortGetHeapStats(&heap_stats);
			snprintf(out_buffer, len, "%d\t\t%d\t%d\r\n", configTOTAL_HEAP_SIZE, heap_stats.xAvailableHeapSpaceInBytes,configTOTAL_HEAP_SIZE - heap_stats.xAvailableHeapSpaceInBytes);
			stage ++;
			return pdPASS;
		default:
			snprintf(out_buffer, len, "\r\n");
			stage = 0;
			return pdFALSE;
	}
}

static const CLI_Command_Definition_t stats = {
	"stats",
	"\r\nstats:\r\n Displays a table showing the state of FreeRTOS\r\n\r\n",
	StatsCommand,
	0, 
};

static BaseType_t SetModelCommand(char *out_buffer, size_t len, const char *command_string) {
	const char *param;
	BaseType_t param_len;
	Robot_ID_t id;
	
	if (out_buffer == NULL)
		return pdFALSE;
	
	param = FreeRTOS_CLIGetParameter(command_string, 1, &param_len);
	
	if (param == NULL)
		return pdFALSE;
	
	len -= 1;
	static uint8_t stage = 0;
	switch (stage) {
		case 0:
			snprintf(out_buffer, len, "Set robot model to: ");
			stage = 1;
			return pdPASS;
		case 1:
			Robot_GetRobotID(&id);
			switch (*param) {
				case 'I':
					snprintf(out_buffer, len, "Infantry.");
					id.model = ROBOT_MODEL_INFANTRY;
					break;
				case 'H':
					snprintf(out_buffer, len, "Hero.");
					id.model = ROBOT_MODEL_HERO;
					break;
				case 'E':
					snprintf(out_buffer, len, "Engineer.");
					id.model = ROBOT_MODEL_ENGINEER;
					break;
				case 'D':
					snprintf(out_buffer, len, "Drone.");
					id.model = ROBOT_MODEL_DRONE;
					break;
				case 'S':
					snprintf(out_buffer, len, "Sentry.");
					id.model = ROBOT_MODEL_SENTRY;
					break;
				default:
					stage = 2;
					return pdPASS;
			}
			Robot_SetRobotID(&id);
			stage = 3;
			return pdPASS;
		case 2:
			snprintf(out_buffer, len, "Unknow model.\r\nCheck help for avaliable options.");
			stage = 4;
			return pdPASS;
		case 3:
			snprintf(out_buffer, len, "\r\nRestart needed for setting to take effect.");
			stage = 4;
			return pdPASS;
		default:
			snprintf(out_buffer, len, "\r\n");
			stage = 0;
			return pdFALSE;
	}
}

static const CLI_Command_Definition_t set_model = {
	"set-model",
	"\r\nset-model <model>:\r\n Set robot model. Expext:I[nfantry], H[ero], E[ngineer], D[rone] and S[entry]\r\n\r\n",
	SetModelCommand,
	1,
};

static BaseType_t SetUserCommand(char *out_buffer, size_t len, const char *command_string) {
	static const char *const qs = "qs";
	
	
	const char *param;
	BaseType_t param_len;
	Robot_ID_t id;
	
	if (out_buffer == NULL)
		return pdFALSE;
	
	param = FreeRTOS_CLIGetParameter(command_string, 1, &param_len);
	
	if (param == NULL)
		return pdFALSE;
	
	len -= 1;
	static uint8_t stage = 0;
	switch (stage) {
		case 0:
			snprintf(out_buffer, len, "Set robot pilot to: ");
			stage = 1;
			return pdPASS;
		case 1:
			Robot_GetRobotID(&id);
			if (strcmp(qs, param) == 0) {
				snprintf(out_buffer, len, "QS.");
				id.pilot = ROBOT_PILOT_QS;
			} else {
				stage = 2;
				return pdPASS;
			}
			Robot_SetRobotID(&id);
			stage = 3;
			return pdPASS;
		case 2:
			snprintf(out_buffer, len, "Unauthorized pilot.\r\nCheck help for avaliable options.");
			stage = 4;
			return pdPASS;
		case 3:
			snprintf(out_buffer, len, "\r\nRestart needed for setting to take effect.");
			stage = 4;
			return pdPASS;
		default:
			snprintf(out_buffer, len, "\r\n");
			stage = 0;
			return pdFALSE;
	}
}

static const CLI_Command_Definition_t set_user = {
	"set-pilot",
	"\r\nset-pilot <pilot>:\r\n Set robot pilot. Expext: QS\r\n\r\n",
	SetUserCommand,
	1,
};


/* Private function ----------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
void Task_CLI(void *argument) {
	(void)argument;
	
	static char input[MAX_INPUT_LENGTH];
	char *output = FreeRTOS_CLIGetOutputBuffer();
	char rx_char;
	uint16_t index = 0;
	BaseType_t processing = 0;
	
	/* Register all the commands. */
	FreeRTOS_CLIRegisterCommand(&endian);
	FreeRTOS_CLIRegisterCommand(&stats);
	FreeRTOS_CLIRegisterCommand(&set_model);
	FreeRTOS_CLIRegisterCommand(&set_user);
	
	/* Init robot. */
	task_param.config_robot = Robot_GetConfigDefault();
	task_param.config_pilot = Robot_GetPilotConfigDefault();
	
	task_param.thread.cli = osThreadGetId();
	
	osKernelLock();
	task_param.thread.command		= osThreadNew(Task_Command,		&task_param, &command_attr);
	task_param.thread.ctrl_chassis	= osThreadNew(Task_CtrlChassis,	&task_param, &ctrl_chassis_attr);
	task_param.thread.ctrl_gimbal	= osThreadNew(Task_CtrlGimbal,	&task_param, &ctrl_gimbal_attr);
	task_param.thread.ctrl_shoot	= osThreadNew(Task_CtrlShoot,	&task_param, &ctrl_shoot_attr);
	task_param.thread.info			= osThreadNew(Task_Info,		&task_param, &info_attr);
	task_param.thread.monitor		= osThreadNew(Task_Monitor,		&task_param, &monitor_attr);
	task_param.thread.atti_esti		= osThreadNew(Task_AttiEsti,	&task_param, &atti_esti_attr);
	task_param.thread.referee		= osThreadNew(Task_Referee,		&task_param, &referee_attr);
	osKernelUnlock();
	
	
	/* Command Line Interface. */
	BSP_USB_Printf("Please press ENTER to activate this console.\r\n");
	while(1) {
		BSP_USB_ReadyReceive(osThreadGetId());
		osThreadFlagsWait(BSP_USB_SIGNAL_BUF_RECV, osFlagsWaitAll, osWaitForever);
		
		rx_char = BSP_USB_ReadChar();
		
		if (rx_char == '\n' || rx_char == '\r') {
		BSP_USB_Printf("%c", rx_char);
			break;
		}
	}
	
	BSP_USB_Printf(CLI_WELCOME_MESSAGE);
	
	BSP_USB_Printf("rm>");
	
	while(1) {
#ifdef DEBUG
		task_param.stack_water_mark.cli = uxTaskGetStackHighWaterMark(NULL);
#endif
		/* Wait for input. */
		BSP_USB_ReadyReceive(osThreadGetId());
		osThreadFlagsWait(BSP_USB_SIGNAL_BUF_RECV, osFlagsWaitAll, osWaitForever);
		
		rx_char = BSP_USB_ReadChar();
		
		if (rx_char <= 126 && rx_char >= 32) {
			if (index < MAX_INPUT_LENGTH) {
				BSP_USB_Printf("%c", rx_char);
				input[index] = rx_char;
				index++;
			}
		} else {
			if (rx_char == '\n' || rx_char == '\r') {\
				BSP_USB_Printf("\r\n");
				if (index > 0) {
					do {
						processing = FreeRTOS_CLIProcessCommand(input, output, configCOMMAND_INT_MAX_OUTPUT_SIZE);
						BSP_USB_Printf(output);
						memset(output, 0x00, MAX_INPUT_LENGTH);
					} while(processing != pdFALSE);
					index = 0;
					memset(input, 0x00, MAX_INPUT_LENGTH);
				}
				BSP_USB_Printf("rm>");
			} else if (rx_char == '\b' || rx_char == 0x7Fu) {
				if (index > 0) {
					BSP_USB_Printf("%c", rx_char);
					index--;
					input[index] = 0;
				}
			}
		}
	}
}
