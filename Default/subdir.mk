################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../voronoi.cpp 

CU_SRCS += \
../voronoi_kernel.cu 

CU_DEPS += \
./voronoi_kernel.d 

OBJS += \
./voronoi.o \
./voronoi_kernel.o 

CPP_DEPS += \
./voronoi.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: NVCC Compiler'
	/usr/local/cuda-5.5/bin/nvcc -DGL_GLEXT_PROTOTYPES -G -g -lineinfo -pg -O3 --use_fast_math -gencode arch=compute_30,code=sm_30 -odir "" -M -o "$(@:%.o=%.d)" "$<"
	/usr/local/cuda-5.5/bin/nvcc -DGL_GLEXT_PROTOTYPES -G -g -lineinfo -pg -O3 --use_fast_math --compile  -x c++ -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

%.o: ../%.cu
	@echo 'Building file: $<'
	@echo 'Invoking: NVCC Compiler'
	/usr/local/cuda-5.5/bin/nvcc -DGL_GLEXT_PROTOTYPES -G -g -lineinfo -pg -O3 --use_fast_math -gencode arch=compute_30,code=sm_30 -odir "" -M -o "$(@:%.o=%.d)" "$<"
	/usr/local/cuda-5.5/bin/nvcc --compile --use_fast_math -DGL_GLEXT_PROTOTYPES -G -O3 -g -gencode arch=compute_30,code=sm_30 -lineinfo -pg  -x cu -o  "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


