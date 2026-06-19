################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../libprim/src/fb.c \
../libprim/src/fill.c \
../libprim/src/glow.c \
../libprim/src/gradient.c \
../libprim/src/path.c \
../libprim/src/prim.c \
../libprim/src/shapes.c \
../libprim/src/text.c 

OBJS += \
./libprim/src/fb.o \
./libprim/src/fill.o \
./libprim/src/glow.o \
./libprim/src/gradient.o \
./libprim/src/path.o \
./libprim/src/prim.o \
./libprim/src/shapes.o \
./libprim/src/text.o 

C_DEPS += \
./libprim/src/fb.d \
./libprim/src/fill.d \
./libprim/src/glow.d \
./libprim/src/gradient.d \
./libprim/src/path.d \
./libprim/src/prim.d \
./libprim/src/shapes.d \
./libprim/src/text.d 


# Each subdirectory must supply rules for building sources it contributes
libprim/src/%.o libprim/src/%.su libprim/src/%.cyclo: ../libprim/src/%.c libprim/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx -DUSE_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO -c -I../Core/Inc -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libui/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/src" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/app" -I../../Middlewares/Third_Party/lvgl -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-libprim-2f-src

clean-libprim-2f-src:
	-$(RM) ./libprim/src/fb.cyclo ./libprim/src/fb.d ./libprim/src/fb.o ./libprim/src/fb.su ./libprim/src/fill.cyclo ./libprim/src/fill.d ./libprim/src/fill.o ./libprim/src/fill.su ./libprim/src/glow.cyclo ./libprim/src/glow.d ./libprim/src/glow.o ./libprim/src/glow.su ./libprim/src/gradient.cyclo ./libprim/src/gradient.d ./libprim/src/gradient.o ./libprim/src/gradient.su ./libprim/src/path.cyclo ./libprim/src/path.d ./libprim/src/path.o ./libprim/src/path.su ./libprim/src/prim.cyclo ./libprim/src/prim.d ./libprim/src/prim.o ./libprim/src/prim.su ./libprim/src/shapes.cyclo ./libprim/src/shapes.d ./libprim/src/shapes.o ./libprim/src/shapes.su ./libprim/src/text.cyclo ./libprim/src/text.d ./libprim/src/text.o ./libprim/src/text.su

.PHONY: clean-libprim-2f-src

