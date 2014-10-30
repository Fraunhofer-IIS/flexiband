# Flexiband API

## USB Interface

### USB [Descriptors](http://www.beyondlogic.org/usbnutshell/usb5.shtml)

The Flexiband USB3.0 device has one [Configuration](http://www.beyondlogic.org/usbnutshell/usb5.shtml#ConfigurationDescriptors) with one Interface. This Interface has three [AlternateSettings](http://www.beyondlogic.org/usbnutshell/usb5.shtml#InterfaceDescriptors) for different transfer rates.
The Interface always has the same [Isochronous Endpoint](http://www.beyondlogic.org/usbnutshell/usb3.shtml#Endpoints), but with different reserved bandwith.

Endpoints are used to communicate with USB devices. See [USB Functions](http://www.beyondlogic.org/usbnutshell/usb3.shtml#USBFunctions). All USB devices have the Endpoint EP0 for [Control Transfers](http://www.beyondlogic.org/usbnutshell/usb4.shtml#Control). These Control Transfers can be used for [Standard Device Requests](http://www.beyondlogic.org/usbnutshell/usb6.shtml#StandardDeviceRequests) and for Vendor Requests. Control Transfers have a limited payload size and are only used to change settings or request status.

The Flexiband also has the Endpoint EP3 for Isochronous Transfers. This Endpoint starts to produce data as soon as the Flexiband was started with the *"Start data transfer"* Vendor Request (see below). For the data format see [Data Format](#data-format).

### USB Vendor Requests

#### FX3-USB-Controller

##### Get Interface-Board Revision

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x00     | 0x00   | -      | 0x01    | Board Revision (1 byte) |

Since FX3 build: 3  
Since Atmel build: -

##### Get FX3 Infos

* Jenkins Build Number
* First eight hex characters of git hash.
* Build time in seconds since 01.01.2000.

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x00     | 0x01   | -      | 0x02    | Build Number (2 byte) |
| 0xC0          | 0x00     | 0x02   | -      | 0x04    | Git Hash (4 byte) |
| 0xC0          | 0x00     | 0x03   | -      | 0x04    | Timestamp (4 byte) |

Since FX3 build: 3  
Since Atmel build: -

##### Start/Stop data transfer

* Yellow LED on Flexiband switched on/off.
* Start=0; Stop=1

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0x40          | 0x00     | &lt;start/stop&gt; | - | 0x00    | - |

Since FX3 build: 3  
Since Atmel build: -

##### Power on/off Base-Board

* on=2; off=3

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0x40          | 0x00     | &lt;on/off&gt; | - | 0x00    | - |

Since FX3 build: 3  
Since Atmel build: -

##### Load FPGA

Load a bitstream into the FPGA. The bitstream has to be loaded page by page. Page loading is started by transferring the first page. Each page is 512 bytes. The last page has to have a &lt;len&gt; less than 512. If The bitstream size is a multiple of 512 bytes, send an additional page with &lt;len&gt;=0.

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0x40          | 0x00     | 0xff00 | &lt;page&gt; | &lt;len&gt; | Data (512 byte) |

##### Hard reset

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0x40          | 0x00     | 0xffff | -      | 0x00    | - |

Since FX3 build: 16  
Since Atmel build: -

#### Atmel

##### Get/Set AutoGainControl

* off=0; on=1

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x01     | -      | 0x20   | 0x01    | &lt;on/off&gt; |
| 0x40          | 0x01     | &lt;on/off&gt; | 0x20 | 0x00 | - |

Since FX3 build: 3  
Since Atmel build: 25

##### Get Base-Board Revision

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x02     | 0x00   | -      | 0x01    | Board Revision (1 byte) |

Since FX3 build: 3  
Since Atmel build: 9

##### Get Atmel Info

* Jenkins Build Number.
* First eight hex characters of git hash.
* Build time in seconds since 01.01.2000.

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x02     | 0x01   | -      | 0x02    | Build Number (2 byte) |
| 0xC0          | 0x02     | 0x02   | -      | 0x04    | Git Hash (4 byte) |
| 0xC0          | 0x02     | 0x03   | -      | 0x04    | Timestamp (4 byte) |

Since FX3 build: 3  
Since Atmel build: 7

#### Base-Board / FPGA

##### Get FPGA Info

* Jenkins Build Number.
* First eight hex characters of git hash.
* Build time in seconds since 01.01.2000.

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x03     | 0x01   | -      | 0x02    | Build Number (2 byte) |
| 0xC0          | 0x03     | 0x02   | -      | 0x04    | Git Hash (4 byte) |
| 0xC0          | 0x03     | 0x03   | -      | 0x04    | Timestamp (4 byte) |

#### RF-Boards

&lt;RF-Slot&gt;: 0 - 2  
This documents describes Layout ID=1

##### Get RF-EEPROM Layout

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x00   | &lt;RF-Slot&gt; | 0x01 | Layout ID (1 byte) |

Since FX3 build: 3  
Since Atmel build: 7

##### Get RF-Board Serial

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x01   | &lt;RF-Slot&gt; | 0x01 | Board Serial (1 byte) |

Since FX3 build: 3  
Since Atmel build: 7

##### Get RF-Board Antenna Number

Antenna Number on Flexiband Housing: 1 - 3

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x02   | &lt;RF-Slot&gt; | 0x01 | Antenna Number (1 byte) |

Since FX3 build: 3  
Since Atmel build: 7

##### Get RF-Board RF-Bandwidth

Analog filter bandwidth in MHz.

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x03   | &lt;RF-Slot&gt; | 0x01 | Bandwidth (1 byte) |

Since FX3 build: 3  
Since Atmel build: 7

##### Get RF-Board Lo Freq

Local oszillator freq in Hz.

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x04   | &lt;RF-Slot&gt; | 0x04 | LO frequency (4 byte) |

Since FX3 build: 3  
Since Atmel build: 7

##### Get RF-Board Band Name

Human readable name; ASCII (i.e. L1/G1).

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x08   | &lt;RF-Slot&gt; | 0x08 | Name (8 byte) |

Since FX3 build: 3  
Since Atmel build: 7

##### Get RF-Board DAC Info

Minimum, maximum meaningful values and default value for the DAC (see below).

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x10   | &lt;RF-Slot&gt; | 0x01 | DAC min (1 byte) |
| 0xC0          | 0x04     | 0x11   | &lt;RF-Slot&gt; | 0x01 | DAC max (1 byte) |
| 0xC0          | 0x04     | 0x12   | &lt;RF-Slot&gt; | 0x01 | DAC default (1 byte) |

Since FX3 build: 3  
Since Atmel build: 7

##### Get/Set RF-Board Antenna Supply default

The default value is applied on startup.
* Rev1 on: 0xFF – off: 0xFD
* Rev2 on: 0xFD – off: 0xFF

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x04     | 0x13   | &lt;RF-Slot&gt; | 0x01 | Ant power default (1 byte) |
| 0x40          | 0x04     | 0x13   | &lt;RF-Slot&gt; | 0x01 | Ant power default (1 byte) |

##### Set RF-Board Antenna Supply

* Rev1 on: 0xFF – off: 0xFD
* Rev2 on: 0xFD – off: 0xFF

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0x40          | 0x05     | &lt;on/off&gt; | &lt;RF-Slot&gt; | 0x00 | - |

##### Get RF-Board Info

* Bit0: Antenna Fault
* Bit1: Antenna Supply
* Bit3-4: Board Revision

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0xC0          | 0x05     | 0x00   | &lt;RF-Slot&gt; | 0x01 | Status (2 bit) + Revision (2 bit) |

##### Set RF-Board Amplification

* &lt;amp&gt;: 0 (min amplification) – 255 (max amplification)
* Should be between *DAC min* and *DAC max*, see **Get RF-Board DAC Info**
* TODO: Amplification in dB (0 – 70)

| bmRequestType | bRequest | wValue | wIndex | wLength | Data |
|---------------|----------|--------|--------|---------|------|
| 0x40          | 0x06     | &lt;amp&gt; | &lt;RF-Slot&gt; | 0x00 | - |

Since FX3 build: 3  
Since Atmel build: 14

## Data Format

### Framing

Data frames are always 1024 bytes long. They contain a preamble, a 32-bit counter, payload data and padding to fill up the frame.

The counter starts at zero when the flexiband is started with the *"Start data transfer"* Vendor Request. It increments with each frame. When the maximum value is reached is rolls over to zero.

The padding is currently filled with 0x00 bytes. In a later version it might be used as CRC.

<table>
  <tr align="center"><td>0</td><td>1</td><td>2</td><td>3</td><td>4</td><td>5</td><td>6</td><td>7</td><td>...</td><td>m</td><td>...</td><td>1023</td></tr>
  <tr><th colspan="2">PREAMBLE</th><th colspan="4">COUNTER</th><th colspan="3">PAYLOAD DATA</th><th colspan="3">PADDING</th></tr>
  <tr align="center"><td>0x55</td><td>0xAA</td><td colspan="4">count</td><td colspan="3"> (up to 1014 bytes) </td><td colspan="3"> 0x00 </td></table>
</table>

### Payload

The layout of the payload depends on the current FPGA configuration. Here are some examples:

<table>
  <tr align="center"><th>byte</th><td colspan="4">0</td><td colspan="4">1</td><td>...</td><td colspan="4">1014</td></tr>
  <tr align="center"><th>I-3</th><td colspan="2">L5 I [7:4]</td><td colspan="2">L5 Q [3:0]</td><td colspan="2">L5 I [7:4]</td><td colspan="2">L5 Q [3:0]</td><td>...</td><td colspan="2">L5 I [7:4]</td><td colspan="2">L5 Q [3:0]</td></tr>
  <tr align="center"><th>III-1a</th><td>L2 I [7:6]</td><td>L2 Q [5:4]</td><td>L1 I [3:2]</td><td>L1 Q [1:0]</td><td colspan="2">L5 I [7:4]</td><td colspan="2">L5 Q [3:0]</td><td>...</td><td colspan="2">L5 I [7:4]</td><td colspan="2">L5 Q [3:0]</td></tr>
</table>