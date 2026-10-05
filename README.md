# Fuse Vault
Fuse Vault is a security hardware device designed to provide a genuinely trustable location where you can store various secret and sensitive documents. It provides encrypted at rest storage and on-device unlocking alongside FIDO2 passkey capabilities.

## Philosophy
Fuse Vault is built around the principle of Trust. Every aspect of Fuse Vault needed to be as open source, inspectable and trustable by the user as possible. The intention was to create a device that could be trusted rather than a device that had to be trusted. Specifically in contrast to things like a TPM. To achieve this, Fuse Vault has used open source hardware where possible and all the software is also open source, inspectable and customizable. Additionally, where trust can't be 100% confirmed, we instead take whatever measures we can to get as close as possible to something we can trust. For example, when encrypting data, we allow users to establish a stack of algorithms each with a varied provenance.

## Hardware
The core of Fuse Vault is the RP2354, a Raspberry Pi RP2350-family microcontroller with integrated flash. Its architecture, memory map and peripheral interfaces are publicly documented, and the Pico SDK and boot ROM source are available. The Hazard3 RISC-V cores are open hardware; the Arm Cortex-M33 cores used by this firmware are not open-source implementations. The whole chip should therefore not be described as open-source hardware. See the [Raspberry Pi chip documentation](https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html) and [boot ROM source](https://github.com/raspberrypi/pico-bootrom-rp2350). There is also a screen and D-pad enabling user interaction with the device without using the host computer. This is intended to ensure the user's key is never exposed to an attacker through a compromised computer. The PCB contains a USB-A and a USB-C male port allowing it to be plugged into most devices. In this revision of the PCB, for simplicity the storage is attached as an SD card. All data on this card is either always encrypted or not sensitive, however this is understood as a potential vulnerability.

## Software
The software is written in C with the Pico SDK and controls and coordinates the rest of the functionality of the device. The software is highly user configurable and designed to be secure over fast with observed speeds around 400 KiB/s on the device display. Throughput depends on the selected cipher stack and workload; this is not a guaranteed rate. One KiB is 1,024 bytes.

### Subsections

- [Key handling](docs/KeyHandling.md)
- [File storage](docs/FileStorage.md)
