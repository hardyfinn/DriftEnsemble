# Builds plugins/DriftEnsembleNT.o for the disting NT.
# Needs the ARM toolchain (arm-none-eabi-gcc) and the disting NT API:
#   git clone https://github.com/expertsleepersltd/distingNT_API
NT_API_PATH ?= ../../distingNT_API

plugins/DriftEnsembleNT.o: DriftEnsembleNT.cpp
	mkdir -p plugins
	arm-none-eabi-c++ -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall -I$(NT_API_PATH)/include -c -o $@ $<

clean:
	rm -rf plugins
