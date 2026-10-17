//! Hardware-independent sensor drivers and motor/USB control logic.
#![no_std]

pub mod mixer_schedule;
pub mod motor_control;
// nextion.rs, nextion_wire.rs and protocol.rs are retained as reference only.
// They are not compiled into the UART-free firmware.
pub mod sensors;
pub mod usb_control;
