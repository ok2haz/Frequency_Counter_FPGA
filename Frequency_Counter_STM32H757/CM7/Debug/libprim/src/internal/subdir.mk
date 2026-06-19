################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../libprim/src/internal/bezier.c \
../libprim/src/internal/dma2d_backend.c \
../libprim/src/internal/rasterizer.c 

OBJS += \
./libprim/src/internal/bezier.o \
./libprim/src/internal/dma2d_backend.o \
./libprim/src/internal/rasterizer.o 

C_DEPS += \
./libprim/src/internal/bezier.d \
./libprim/src/internal/dma2d_backend.d \
./libprim/src/internal/rasterizer.d 


# Each subdirectory must supply rules for building sources it contributes
libprim/src/internal/%.o libprim/src/internal/%.su libprim/src/internal/%.cyclo: ../libprim/src/internal/%.c libprim/src/internal/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx -DUSE_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO -c -I../Core/Inc -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libui/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/src" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/app" -I../../Middlewares/Third_Party/lvgl -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-libprim-2f-src-2f-internal

clean-libprim-2f-src-2f-internal:
	-$(RM) ./libprim/src/internal/bezier.cyclo ./libprim/src/internal/bezier.d ./libprim/src/internal/bezier.o ./libprim/src/internal/bezier.su ./libprim/src/internal/dma2d_backend.cyclo ./libprim/src/internal/dma2d_backend.d ./libprim/src/internal/dma2d_backend.o ./libprim/src/internal/dma2d_backend.su ./libprim/src/internal/rasterizer.cyclo ./libprim/src/internal/rasterizer.d ./libprim/src/internal/rasterizer.o ./libprim/src/internal/rasterizer.su

.PHONY: clean-libprim-2f-src-2f-internal

