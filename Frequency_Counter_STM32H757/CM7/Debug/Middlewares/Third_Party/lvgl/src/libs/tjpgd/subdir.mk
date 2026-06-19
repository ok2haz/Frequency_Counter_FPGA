################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.c \
C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.c 

OBJS += \
./Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.o \
./Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.o 

C_DEPS += \
./Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.d \
./Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.o: C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.c Middlewares/Third_Party/lvgl/src/libs/tjpgd/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx -DUSE_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO -DLV_CONF_INCLUDE_SIMPLE=LV_CONF_INCLUDE_SIMPLE -c -I../Core/Inc -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libui/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/src" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/app" -I../../Middlewares/Third_Party/lvgl -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.o: C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.c Middlewares/Third_Party/lvgl/src/libs/tjpgd/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx -DUSE_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO -DLV_CONF_INCLUDE_SIMPLE=LV_CONF_INCLUDE_SIMPLE -c -I../Core/Inc -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libui/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/src" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/app" -I../../Middlewares/Third_Party/lvgl -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middlewares-2f-Third_Party-2f-lvgl-2f-src-2f-libs-2f-tjpgd

clean-Middlewares-2f-Third_Party-2f-lvgl-2f-src-2f-libs-2f-tjpgd:
	-$(RM) ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.cyclo ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.d ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.o ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/lv_tjpgd.su ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.cyclo ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.d ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.o ./Middlewares/Third_Party/lvgl/src/libs/tjpgd/tjpgd.su

.PHONY: clean-Middlewares-2f-Third_Party-2f-lvgl-2f-src-2f-libs-2f-tjpgd

