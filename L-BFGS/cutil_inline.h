#include "cuda.h"
#include "cuda_runtime.h"
#include "device_launch_parameters.h"

//
//#define cutilCheckMsg(a) getLastCudaError(a)
//#define cutGetMaxGflopsDeviceId() gpuGetMaxGflopsDeviceId()
//
#define __min(a,b) (a) < (b) ? (a) : (b)
#define __max(a,b) (a) > (b) ? (a) : (b)
