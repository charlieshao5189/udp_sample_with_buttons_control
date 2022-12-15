# udp_sample_with_buttons_control
Add button/switch control to the original NCS2.2.0 nRF9160: UDP sample, also demostrate how to use different RAI methods.

To use Release Assistance Indication (RAI) there are two approaches:
1. CP-RAI (NB-IoT only) enabled by using the AT%XRAI command. See branch: https://github.com/charlieshao5189/udp_sample_with_buttons_control/tree/udp_xrai
2. AS-RAI (LTE-M and NB-IoT) enabled by using the AT%RAI command in combination with socketopt to signal when the device has sent its last packet. See branch: https://github.com/charlieshao5189/udp_sample_with_buttons_control/tree/udp_rel14_rai
