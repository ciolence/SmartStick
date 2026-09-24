#include "stm32f1xx_hal.h"

#include "Delay.h"

GPIO_PinState read_KeyState(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
	GPIO_PinState keyState = GPIO_PIN_SET;
	if (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == GPIO_PIN_RESET) {
		HAL_Delay(20);
		keyState = GPIO_PIN_RESET;
	}
	return keyState;
}











