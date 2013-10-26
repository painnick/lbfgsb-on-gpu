################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../L-BFGS/ap.cpp \
../L-BFGS/lbfgsb.cpp 

CU_SRCS += \
../L-BFGS/active.cu \
../L-BFGS/bmv.cu \
../L-BFGS/cauchy.cu \
../L-BFGS/cmprlb.cu \
../L-BFGS/dpofa.cu \
../L-BFGS/formk.cu \
../L-BFGS/formt.cu \
../L-BFGS/lnsrlb.cu \
../L-BFGS/matupd.cu \
../L-BFGS/minimize.cu \
../L-BFGS/projgr.cu \
../L-BFGS/subsm.cu 

CU_DEPS += \
./L-BFGS/active.d \
./L-BFGS/bmv.d \
./L-BFGS/cauchy.d \
./L-BFGS/cmprlb.d \
./L-BFGS/dpofa.d \
./L-BFGS/formk.d \
./L-BFGS/formt.d \
./L-BFGS/lnsrlb.d \
./L-BFGS/matupd.d \
./L-BFGS/minimize.d \
./L-BFGS/projgr.d \
./L-BFGS/subsm.d 

OBJS += \
./L-BFGS/active.o \
./L-BFGS/ap.o \
./L-BFGS/bmv.o \
./L-BFGS/cauchy.o \
./L-BFGS/cmprlb.o \
./L-BFGS/dpofa.o \
./L-BFGS/formk.o \
./L-BFGS/formt.o \
./L-BFGS/lbfgsb.o \
./L-BFGS/lnsrlb.o \
./L-BFGS/matupd.o \
./L-BFGS/minimize.o \
./L-BFGS/projgr.o \
./L-BFGS/subsm.o 

CPP_DEPS += \
./L-BFGS/ap.d \
./L-BFGS/lbfgsb.d 


# Each subdirectory must supply rules for building sources it contributes
L-BFGS/%.o: ../L-BFGS/%.cu
	@echo 'Building file: $<'
	@echo 'Invoking: NVCC Compiler'
	/usr/local/cuda-5.5/bin/nvcc -DGL_GLEXT_PROTOTYPES -G -g -lineinfo -pg -O2 -gencode arch=compute_30,code=sm_30 -odir "L-BFGS" -M -o "$(@:%.o=%.d)" "$<"
	/usr/local/cuda-5.5/bin/nvcc --compile -DGL_GLEXT_PROTOTYPES -G -O2 -g -gencode arch=compute_30,code=compute_30 -gencode arch=compute_30,code=sm_30 -lineinfo -pg  -x cu -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

L-BFGS/%.o: ../L-BFGS/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: NVCC Compiler'
	/usr/local/cuda-5.5/bin/nvcc -DGL_GLEXT_PROTOTYPES -G -g -lineinfo -pg -O2 -gencode arch=compute_30,code=sm_30 -odir "L-BFGS" -M -o "$(@:%.o=%.d)" "$<"
	/usr/local/cuda-5.5/bin/nvcc -DGL_GLEXT_PROTOTYPES -G -g -lineinfo -pg -O2 --compile  -x c++ -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


