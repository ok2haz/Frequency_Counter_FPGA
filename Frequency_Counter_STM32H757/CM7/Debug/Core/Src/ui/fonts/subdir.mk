################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/ui/fonts/inter_med_20.c \
../Core/Src/ui/fonts/inter_reg_10.c \
../Core/Src/ui/fonts/inter_reg_14.c \
../Core/Src/ui/fonts/inter_reg_16.c \
../Core/Src/ui/fonts/jbmono_bold_20.c \
../Core/Src/ui/fonts/jbmono_bold_25.c \
../Core/Src/ui/fonts/jbmono_med_18.c \
../Core/Src/ui/fonts/jbmono_med_48.c \
../Core/Src/ui/fonts/jbmono_med_78.c \
../Core/Src/ui/fonts/jbmono_reg_14.c \
../Core/Src/ui/fonts/jbmono_reg_17.c \
../Core/Src/ui/fonts/jbmono_reg_20.c \
../Core/Src/ui/fonts/jbmono_reg_21.c \
../Core/Src/ui/fonts/jbmono_reg_24.c \
../Core/Src/ui/fonts/jbmono_semi_25.c 

OBJS += \
./Core/Src/ui/fonts/inter_med_20.o \
./Core/Src/ui/fonts/inter_reg_10.o \
./Core/Src/ui/fonts/inter_reg_14.o \
./Core/Src/ui/fonts/inter_reg_16.o \
./Core/Src/ui/fonts/jbmono_bold_20.o \
./Core/Src/ui/fonts/jbmono_bold_25.o \
./Core/Src/ui/fonts/jbmono_med_18.o \
./Core/Src/ui/fonts/jbmono_med_48.o \
./Core/Src/ui/fonts/jbmono_med_78.o \
./Core/Src/ui/fonts/jbmono_reg_14.o \
./Core/Src/ui/fonts/jbmono_reg_17.o \
./Core/Src/ui/fonts/jbmono_reg_20.o \
./Core/Src/ui/fonts/jbmono_reg_21.o \
./Core/Src/ui/fonts/jbmono_reg_24.o \
./Core/Src/ui/fonts/jbmono_semi_25.o 

C_DEPS += \
./Core/Src/ui/fonts/inter_med_20.d \
./Core/Src/ui/fonts/inter_reg_10.d \
./Core/Src/ui/fonts/inter_reg_14.d \
./Core/Src/ui/fonts/inter_reg_16.d \
./Core/Src/ui/fonts/jbmono_bold_20.d \
./Core/Src/ui/fonts/jbmono_bold_25.d \
./Core/Src/ui/fonts/jbmono_med_18.d \
./Core/Src/ui/fonts/jbmono_med_48.d \
./Core/Src/ui/fonts/jbmono_med_78.d \
./Core/Src/ui/fonts/jbmono_reg_14.d \
./Core/Src/ui/fonts/jbmono_reg_17.d \
./Core/Src/ui/fonts/jbmono_reg_20.d \
./Core/Src/ui/fonts/jbmono_reg_21.d \
./Core/Src/ui/fonts/jbmono_reg_24.d \
./Core/Src/ui/fonts/jbmono_semi_25.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/ui/fonts/%.o Core/Src/ui/fonts/%.su Core/Src/ui/fonts/%.cyclo: ../Core/Src/ui/fonts/%.c Core/Src/ui/fonts/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx -DUSE_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO -c -I../Core/Inc -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libui/include" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/libprim/src" -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/CM7/app" -I../../Middlewares/Third_Party/lvgl -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -I"C:/Users/Latitude/STM32CubeIDE/workspace_2.1.0/H757_LED/H757_LED/Middlewares/Third_Party/lvgl" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-ui-2f-fonts

clean-Core-2f-Src-2f-ui-2f-fonts:
	-$(RM) ./Core/Src/ui/fonts/inter_med_20.cyclo ./Core/Src/ui/fonts/inter_med_20.d ./Core/Src/ui/fonts/inter_med_20.o ./Core/Src/ui/fonts/inter_med_20.su ./Core/Src/ui/fonts/inter_reg_10.cyclo ./Core/Src/ui/fonts/inter_reg_10.d ./Core/Src/ui/fonts/inter_reg_10.o ./Core/Src/ui/fonts/inter_reg_10.su ./Core/Src/ui/fonts/inter_reg_14.cyclo ./Core/Src/ui/fonts/inter_reg_14.d ./Core/Src/ui/fonts/inter_reg_14.o ./Core/Src/ui/fonts/inter_reg_14.su ./Core/Src/ui/fonts/inter_reg_16.cyclo ./Core/Src/ui/fonts/inter_reg_16.d ./Core/Src/ui/fonts/inter_reg_16.o ./Core/Src/ui/fonts/inter_reg_16.su ./Core/Src/ui/fonts/jbmono_bold_20.cyclo ./Core/Src/ui/fonts/jbmono_bold_20.d ./Core/Src/ui/fonts/jbmono_bold_20.o ./Core/Src/ui/fonts/jbmono_bold_20.su ./Core/Src/ui/fonts/jbmono_bold_25.cyclo ./Core/Src/ui/fonts/jbmono_bold_25.d ./Core/Src/ui/fonts/jbmono_bold_25.o ./Core/Src/ui/fonts/jbmono_bold_25.su ./Core/Src/ui/fonts/jbmono_med_18.cyclo ./Core/Src/ui/fonts/jbmono_med_18.d ./Core/Src/ui/fonts/jbmono_med_18.o ./Core/Src/ui/fonts/jbmono_med_18.su ./Core/Src/ui/fonts/jbmono_med_48.cyclo ./Core/Src/ui/fonts/jbmono_med_48.d ./Core/Src/ui/fonts/jbmono_med_48.o ./Core/Src/ui/fonts/jbmono_med_48.su ./Core/Src/ui/fonts/jbmono_med_78.cyclo ./Core/Src/ui/fonts/jbmono_med_78.d ./Core/Src/ui/fonts/jbmono_med_78.o ./Core/Src/ui/fonts/jbmono_med_78.su ./Core/Src/ui/fonts/jbmono_reg_14.cyclo ./Core/Src/ui/fonts/jbmono_reg_14.d ./Core/Src/ui/fonts/jbmono_reg_14.o ./Core/Src/ui/fonts/jbmono_reg_14.su ./Core/Src/ui/fonts/jbmono_reg_17.cyclo ./Core/Src/ui/fonts/jbmono_reg_17.d ./Core/Src/ui/fonts/jbmono_reg_17.o ./Core/Src/ui/fonts/jbmono_reg_17.su ./Core/Src/ui/fonts/jbmono_reg_20.cyclo ./Core/Src/ui/fonts/jbmono_reg_20.d ./Core/Src/ui/fonts/jbmono_reg_20.o ./Core/Src/ui/fonts/jbmono_reg_20.su ./Core/Src/ui/fonts/jbmono_reg_21.cyclo ./Core/Src/ui/fonts/jbmono_reg_21.d ./Core/Src/ui/fonts/jbmono_reg_21.o ./Core/Src/ui/fonts/jbmono_reg_21.su ./Core/Src/ui/fonts/jbmono_reg_24.cyclo ./Core/Src/ui/fonts/jbmono_reg_24.d ./Core/Src/ui/fonts/jbmono_reg_24.o ./Core/Src/ui/fonts/jbmono_reg_24.su ./Core/Src/ui/fonts/jbmono_semi_25.cyclo ./Core/Src/ui/fonts/jbmono_semi_25.d ./Core/Src/ui/fonts/jbmono_semi_25.o ./Core/Src/ui/fonts/jbmono_semi_25.su

.PHONY: clean-Core-2f-Src-2f-ui-2f-fonts

