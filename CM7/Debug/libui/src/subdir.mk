################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../libui/src/bargraph.c \
../libui/src/big_number.c \
../libui/src/button.c \
../libui/src/card.c \
../libui/src/chart.c \
../libui/src/digit_group.c \
../libui/src/icons.c \
../libui/src/pill.c \
../libui/src/sparkline.c \
../libui/src/ui.c 

OBJS += \
./libui/src/bargraph.o \
./libui/src/big_number.o \
./libui/src/button.o \
./libui/src/card.o \
./libui/src/chart.o \
./libui/src/digit_group.o \
./libui/src/icons.o \
./libui/src/pill.o \
./libui/src/sparkline.o \
./libui/src/ui.o 

C_DEPS += \
./libui/src/bargraph.d \
./libui/src/big_number.d \
./libui/src/button.d \
./libui/src/card.d \
./libui/src/chart.d \
./libui/src/digit_group.d \
./libui/src/icons.d \
./libui/src/pill.d \
./libui/src/sparkline.d \
./libui/src/ui.d 


# Each subdirectory must supply rules for building sources it contributes
libui/src/%.o libui/src/%.su libui/src/%.cyclo: ../libui/src/%.c libui/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx -DUSE_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO -c -I../Core/Inc -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libui/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/src" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/app" -I../../Middlewares/Third_Party/lvgl -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-libui-2f-src

clean-libui-2f-src:
	-$(RM) ./libui/src/bargraph.cyclo ./libui/src/bargraph.d ./libui/src/bargraph.o ./libui/src/bargraph.su ./libui/src/big_number.cyclo ./libui/src/big_number.d ./libui/src/big_number.o ./libui/src/big_number.su ./libui/src/button.cyclo ./libui/src/button.d ./libui/src/button.o ./libui/src/button.su ./libui/src/card.cyclo ./libui/src/card.d ./libui/src/card.o ./libui/src/card.su ./libui/src/chart.cyclo ./libui/src/chart.d ./libui/src/chart.o ./libui/src/chart.su ./libui/src/digit_group.cyclo ./libui/src/digit_group.d ./libui/src/digit_group.o ./libui/src/digit_group.su ./libui/src/icons.cyclo ./libui/src/icons.d ./libui/src/icons.o ./libui/src/icons.su ./libui/src/pill.cyclo ./libui/src/pill.d ./libui/src/pill.o ./libui/src/pill.su ./libui/src/sparkline.cyclo ./libui/src/sparkline.d ./libui/src/sparkline.o ./libui/src/sparkline.su ./libui/src/ui.cyclo ./libui/src/ui.d ./libui/src/ui.o ./libui/src/ui.su

.PHONY: clean-libui-2f-src

