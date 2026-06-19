################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../libui/src/fonts/ui_font_mono_14.c \
../libui/src/fonts/ui_font_mono_16.c \
../libui/src/fonts/ui_font_mono_18.c \
../libui/src/fonts/ui_font_mono_20.c \
../libui/src/fonts/ui_font_mono_25.c \
../libui/src/fonts/ui_font_mono_30.c \
../libui/src/fonts/ui_font_mono_52.c \
../libui/src/fonts/ui_font_mono_75.c \
../libui/src/fonts/ui_font_sans_14.c \
../libui/src/fonts/ui_font_sans_16.c \
../libui/src/fonts/ui_font_sans_17.c \
../libui/src/fonts/ui_font_sans_20.c \
../libui/src/fonts/ui_font_sans_32.c 

OBJS += \
./libui/src/fonts/ui_font_mono_14.o \
./libui/src/fonts/ui_font_mono_16.o \
./libui/src/fonts/ui_font_mono_18.o \
./libui/src/fonts/ui_font_mono_20.o \
./libui/src/fonts/ui_font_mono_25.o \
./libui/src/fonts/ui_font_mono_30.o \
./libui/src/fonts/ui_font_mono_52.o \
./libui/src/fonts/ui_font_mono_75.o \
./libui/src/fonts/ui_font_sans_14.o \
./libui/src/fonts/ui_font_sans_16.o \
./libui/src/fonts/ui_font_sans_17.o \
./libui/src/fonts/ui_font_sans_20.o \
./libui/src/fonts/ui_font_sans_32.o 

C_DEPS += \
./libui/src/fonts/ui_font_mono_14.d \
./libui/src/fonts/ui_font_mono_16.d \
./libui/src/fonts/ui_font_mono_18.d \
./libui/src/fonts/ui_font_mono_20.d \
./libui/src/fonts/ui_font_mono_25.d \
./libui/src/fonts/ui_font_mono_30.d \
./libui/src/fonts/ui_font_mono_52.d \
./libui/src/fonts/ui_font_mono_75.d \
./libui/src/fonts/ui_font_sans_14.d \
./libui/src/fonts/ui_font_sans_16.d \
./libui/src/fonts/ui_font_sans_17.d \
./libui/src/fonts/ui_font_sans_20.d \
./libui/src/fonts/ui_font_sans_32.d 


# Each subdirectory must supply rules for building sources it contributes
libui/src/fonts/%.o libui/src/fonts/%.su libui/src/fonts/%.cyclo: ../libui/src/fonts/%.c libui/src/fonts/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx -DUSE_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO -c -I../Core/Inc -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libui/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/src" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/app" -I../../Middlewares/Third_Party/lvgl -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-libui-2f-src-2f-fonts

clean-libui-2f-src-2f-fonts:
	-$(RM) ./libui/src/fonts/ui_font_mono_14.cyclo ./libui/src/fonts/ui_font_mono_14.d ./libui/src/fonts/ui_font_mono_14.o ./libui/src/fonts/ui_font_mono_14.su ./libui/src/fonts/ui_font_mono_16.cyclo ./libui/src/fonts/ui_font_mono_16.d ./libui/src/fonts/ui_font_mono_16.o ./libui/src/fonts/ui_font_mono_16.su ./libui/src/fonts/ui_font_mono_18.cyclo ./libui/src/fonts/ui_font_mono_18.d ./libui/src/fonts/ui_font_mono_18.o ./libui/src/fonts/ui_font_mono_18.su ./libui/src/fonts/ui_font_mono_20.cyclo ./libui/src/fonts/ui_font_mono_20.d ./libui/src/fonts/ui_font_mono_20.o ./libui/src/fonts/ui_font_mono_20.su ./libui/src/fonts/ui_font_mono_25.cyclo ./libui/src/fonts/ui_font_mono_25.d ./libui/src/fonts/ui_font_mono_25.o ./libui/src/fonts/ui_font_mono_25.su ./libui/src/fonts/ui_font_mono_30.cyclo ./libui/src/fonts/ui_font_mono_30.d ./libui/src/fonts/ui_font_mono_30.o ./libui/src/fonts/ui_font_mono_30.su ./libui/src/fonts/ui_font_mono_52.cyclo ./libui/src/fonts/ui_font_mono_52.d ./libui/src/fonts/ui_font_mono_52.o ./libui/src/fonts/ui_font_mono_52.su ./libui/src/fonts/ui_font_mono_75.cyclo ./libui/src/fonts/ui_font_mono_75.d ./libui/src/fonts/ui_font_mono_75.o ./libui/src/fonts/ui_font_mono_75.su ./libui/src/fonts/ui_font_sans_14.cyclo ./libui/src/fonts/ui_font_sans_14.d ./libui/src/fonts/ui_font_sans_14.o ./libui/src/fonts/ui_font_sans_14.su ./libui/src/fonts/ui_font_sans_16.cyclo ./libui/src/fonts/ui_font_sans_16.d ./libui/src/fonts/ui_font_sans_16.o ./libui/src/fonts/ui_font_sans_16.su ./libui/src/fonts/ui_font_sans_17.cyclo ./libui/src/fonts/ui_font_sans_17.d ./libui/src/fonts/ui_font_sans_17.o ./libui/src/fonts/ui_font_sans_17.su ./libui/src/fonts/ui_font_sans_20.cyclo ./libui/src/fonts/ui_font_sans_20.d ./libui/src/fonts/ui_font_sans_20.o ./libui/src/fonts/ui_font_sans_20.su ./libui/src/fonts/ui_font_sans_32.cyclo ./libui/src/fonts/ui_font_sans_32.d ./libui/src/fonts/ui_font_sans_32.o ./libui/src/fonts/ui_font_sans_32.su

.PHONY: clean-libui-2f-src-2f-fonts

