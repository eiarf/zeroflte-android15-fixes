
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

namespace android {

	extern "C" void _ZN7android8String16aSEOS0_();
	extern "C" void _ZN7android8String16aSEOS0_(){}

	extern "C" void _ZN7android8String16C1EOS0_();
	extern "C" void _ZN7android8String16C1EOS0_(){}

	extern "C" void _ZN7android9SingletonINS_13SensorManagerEE5sLockE();
	extern "C" void _ZN7android9SingletonINS_13SensorManagerEE5sLockE(){}

	extern "C" void _ZN7android9SingletonINS_13SensorManagerEE9sInstanceE();
	extern "C" void _ZN7android9SingletonINS_13SensorManagerEE9sInstanceE(){}

	extern "C" void _ZN7android13SensorManager16createEventQueueEv();
	extern "C" void _ZN7android13SensorManager16createEventQueueEv(){}

	extern "C" void _ZN7android13SensorManagerC1Ev();
	extern "C" void _ZN7android13SensorManagerC1Ev(){}

	// libsensor-mod (gpsd) only takes these two from libpermission, a
	// system library vendor code can't load on Android 17. No AppOps
	// op maps to its permission.
	extern "C" void _ZN7android13AppOpsManagerC1Ev(void *);
	extern "C" void _ZN7android13AppOpsManagerC1Ev(void *){}

	extern "C" int32_t _ZN7android13AppOpsManager18permissionToOpCodeERKNS_8String16E(void *, const void *);
	extern "C" int32_t _ZN7android13AppOpsManager18permissionToOpCodeERKNS_8String16E(void *, const void *){ return -1; }

	extern "C" void* _ZN7android21IPermissionController11asInterfaceERKNS_2spINS_7IBinderEEE(const void *);
	extern "C" void* _ZN7android21IPermissionController11asInterfaceERKNS_2spINS_7IBinderEEE(const void *){ return nullptr; }

}
