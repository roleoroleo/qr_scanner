# qr_scanner
Simple qr-code scanner for yi cam with yi-hack-allwinner: https://github.com/roleoroleo/yi-hack-Allwinner

This sample program allows you to turn your cam into a qr-code scanner.

Based on this library: https://github.com/dlbeer/quirc

The app performs the following steps:
- reads from the h264 buffer
- gets an i-frame
- converts it to a gray scale image
- fills the quirc buffer
- prints to stdout the decoded string or an error
