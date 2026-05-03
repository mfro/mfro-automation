use std::{
    ffi::c_void,
    sync::atomic::{AtomicUsize, Ordering},
};

use windows::{
    Win32::{Foundation::*, System::Com::*},
    core::implement,
};
use windows_core::{BOOL, GUID, HRESULT, IUnknown, Interface, Ref, Result};

mod credentials;
mod util;
mod connect;

use credentials::MyProvider;
use util::log;

const MY_CLASS_ID: GUID = GUID::from_u128(0xece4d7a5_17f9_496c_8450_a490f033e0ae);

static GLOBAL_REF: AtomicUsize = AtomicUsize::new(0);

pub fn global_ref_add() {
    GLOBAL_REF.fetch_add(1, Ordering::SeqCst);
}

pub fn global_ref_release() {
    GLOBAL_REF.fetch_sub(1, Ordering::SeqCst);
}

#[unsafe(export_name = "DllCanUnloadNow")]
extern "C" fn dll_can_unload_now() -> HRESULT {
    log!("DllCanUnloadNow {}", GLOBAL_REF.load(Ordering::SeqCst));

    if GLOBAL_REF.load(Ordering::SeqCst) > 0 {
        S_FALSE
    } else {
        S_OK
    }
}

#[unsafe(export_name = "DllGetClassObject")]
extern "C" fn dll_get_class_object(
    class_id: &GUID,
    interface_id: &GUID,
    out: *mut *mut c_void,
) -> HRESULT {
    log!("DllGetClassObject {:?} {:?}", class_id, interface_id);

    if *class_id == MY_CLASS_ID {
        unsafe { IUnknown::from(MyClassFactory).query(interface_id, out) }
    } else {
        CLASS_E_CLASSNOTAVAILABLE
    }
}

#[implement(IClassFactory)]
struct MyClassFactory;

impl IClassFactory_Impl for MyClassFactory_Impl {
    fn CreateInstance(
        &self,
        outer: Ref<IUnknown>,
        interface_id: *const GUID,
        out: *mut *mut core::ffi::c_void,
    ) -> Result<()> {
        log!("creating instance {:?}", unsafe { *interface_id });

        if outer.is_none() {
            unsafe {
                IUnknown::from(MyProvider::new())
                    .query(interface_id, out)
                    .ok()
            }
        } else {
            CLASS_E_NOAGGREGATION.ok()
        }
    }

    fn LockServer(&self, lock: BOOL) -> Result<()> {
        if lock.as_bool() {
            global_ref_add();
        } else {
            global_ref_release();
        }

        Ok(())
    }
}
