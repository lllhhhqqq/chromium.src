// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/usb/usb_device.h"

#include "base/guid.h"
#include "device/usb/webusb_descriptors.h"

namespace device {

UsbDevice::Observer::~Observer() {}

void UsbDevice::Observer::OnDeviceRemoved(scoped_refptr<UsbDevice> device) {}

UsbDevice::UsbDevice(uint16_t usb_version,
                     uint8_t device_class,
                     uint8_t device_subclass,
                     uint8_t device_protocol,
                     uint16_t vendor_id,
                     uint16_t product_id,
                     uint16_t device_version,
                     const base::string16& manufacturer_string,
                     const base::string16& product_string,
                     const base::string16& serial_number)
    : manufacturer_string_(manufacturer_string),
      product_string_(product_string),
      serial_number_(serial_number),
      guid_(base::GenerateGUID()),
      usb_version_(usb_version),
      device_class_(device_class),
      device_subclass_(device_subclass),
      device_protocol_(device_protocol),
      vendor_id_(vendor_id),
      product_id_(product_id),
      device_version_(device_version) {}

UsbDevice::~UsbDevice() {
}

void UsbDevice::CheckUsbAccess(const ResultCallback& callback) {
  // By default assume that access to the device is allowed. This is implemented
  // on Chrome OS by checking with permission_broker.
  callback.Run(true);
}

void UsbDevice::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void UsbDevice::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void UsbDevice::NotifyDeviceRemoved() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnDeviceRemoved(this));
}

}  // namespace device
