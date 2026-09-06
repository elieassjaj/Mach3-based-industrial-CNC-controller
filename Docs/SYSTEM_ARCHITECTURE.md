# System Architecture

This document illustrates the structural data flow and hardware architecture of the Mach3-Based CNC Controller.

```text
Mach3
  │
  │ Mach3 Plugin
  ▼
PC Ethernet
  │
  │ TCP/UDP ?
  ▼
LAN8720A
  │ RMII
  ▼
STM32F407
  │
  ├── STEP/DIR → X
  ├── STEP/DIR → Y
  ├── STEP/DIR → Z
  ├── STEP/DIR → A
  ├── STEP/DIR → B
  │
  ├── Inputs
  ├── Outputs
  ├── E-Stop
  └── Spindle
```
