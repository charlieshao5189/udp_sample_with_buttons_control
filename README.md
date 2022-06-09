# udp_sample_with_buttons_control
Add button/switch control to the original NCS2.0.0 nRF9160: UDP sample

## Features Explanation:
The sample will behave the same as the original UDP sample: sending one UDP package every 15 minutes by default with no action needed. Following features are added with button/switch actions:

* Pressing “Button 1” will send an UDP packet
* Pressing “Button 2”  will put the modem in Offline mode, pressing button 2 again will reconnect the cellular network.

* Switch 1 set to “GND” = Default behavior of the Sample. PSM enabled
* Switch 1 set to “N.C” = Disable PSM. 
* Switch 2 set to “GND” = Default behavior of the Sample. RAI enabled. This feature is only available for NB-IoT network.
* Switch 2 set to “N.C” = Disable RAI.

For PSM and RAI features, when device is connected with network, it will enable the new features configuration right away; when devcie is offline, it will enable the new features configuraiton when reconnect with network.

## Low Power Evaluation Results
Following [How to Power Profile your cellular IoT application](https://youtu.be/r_dr3Qd8inE), the tests measured the floor current(uA) when device is in PSM mode.
Network configuration for prj.conf are:
  
**LTE-M:**

CONFIG_SERIAL=n 
  
CONFIG_LTE_NETWORK_MODE_LTE_M=y 
  
#CONFIG_LTE_NETWORK_MODE_NBIOT=y 
  

  **NB-IoT:** 
 
CONFIG_SERIAL=n 
  
#CONFIG_LTE_NETWORK_MODE_LTE_M=y 
  
CONFIG_LTE_NETWORK_MODE_NBIOT=y 
  

The floor current(uA) of device under PSM mode:
|                          | LTE-M | NB-IoT |
|--------------------------|-------|--------|
| UDP original sample      | 2.51  |  2.50  |
| UDP with buttons control | 2.58  |  2.56  |
