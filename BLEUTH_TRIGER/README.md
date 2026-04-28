# Trigger Node (Arduino + Dual IR)

This folder contains the C++ firmware for the physical trigger node of THE GARDIANT system.

## Hardware Required
* Arduino Uno
* 2x Infrared Obstacle Avoidance Sensors (Active Low)
* 1x HC-05 Bluetooth Module
* 1x 1kΩ Resistor
* 1x 2kΩ Resistor

## Wiring Architecture

```mermaid
flowchart LR
    %% Define Styles
    classDef power fill:#ffcccc,stroke:#ff0000,stroke-width:2px;
    classDef ground fill:#cccccc,stroke:#000000,stroke-width:2px;
    classDef signal fill:#e6f2ff,stroke:#0066cc,stroke-width:2px;
    classDef passive fill:#ffffcc,stroke:#cccc00,stroke-width:2px;

    subgraph ARDUINO UNO [Arduino Uno Brain]
        PWR5V((5V)):::power
        GND_UNO((GND)):::ground
        D2([Pin 2]):::signal
        D3([Pin 3]):::signal
        D10([Pin 10 - RX]):::signal
        D11([Pin 11 - TX]):::signal
    end

    subgraph SENSOR A [Outer IR Sensor]
        VCC_A((VCC)):::power
        GND_A((GND)):::ground
        DO_A([DO]):::signal
    end

    subgraph SENSOR B [Inner IR Sensor]
        VCC_B((VCC)):::power
        GND_B((GND)):::ground
        DO_B([DO]):::signal
    end
    
    subgraph VOLTAGE DIVIDER [3.3V Logic Protection]
        R1[R1: 1kΩ]:::passive
        R2[R2: 2kΩ]:::passive
    end

    subgraph BLUETOOTH [HC-05 Module]
        VCC_BT((VCC)):::power
        GND_BT((GND)):::ground
        TX_BT([TXD]):::signal
        RX_BT([RXD]):::signal
    end

    %% Power Connections
    PWR5V -- Red Wire --- VCC_A
    PWR5V -- Red Wire --- VCC_B
    PWR5V -- Red Wire --- VCC_BT

    GND_UNO -- Black Wire --- GND_A
    GND_UNO -- Black Wire --- GND_B
    GND_UNO -- Black Wire --- GND_BT

    %% Sensor Logic Connections
    DO_A -- Green Wire --> D2
    DO_B -- Blue Wire --> D3

    %% Bluetooth Logic Connections
    TX_BT -- Yellow Wire --> D10
    
    %% Voltage Divider Routing
    D11 -- Orange Wire: 5V --> R1
    R1 -- Safe Logic: 3.33V --> RX_BT
    R1 -. Tied to Ground .-> R2
    R2 -.-> GND_BT