# PowerManager Plugin Architecture

## Overview

The PowerManager plugin is a comprehensive power state management system for WPEFramework/Thunder, providing centralized control over device power states, thermal management, deep sleep functionality, and system reboot operations. The plugin follows a modular controller-based architecture with well-defined interfaces and separation of concerns.

## System Architecture

### Core Components

#### PowerManager Plugin Entry Point
- **PowerManager**: Main plugin class that implements WPEFramework plugin interfaces
- **PowerManagerImplementation**: Core implementation class providing the IPowerManager interface
- Manages plugin lifecycle, service registration, and client communication through Thunder framework

#### Controller Layer
The plugin employs a specialized controller pattern with four distinct controllers:

1. **PowerController**: Manages power state transitions (ON, STANDBY, STANDBY_LIGHT_SLEEP, STANDBY_DEEP_SLEEP, OFF)
   - Handles power state validation and transitions
   - Integrates with hardware abstraction layer (HAL)
   - Manages wakeup source configuration
   - Processes power mode change requests with client acknowledgment support

2. **DeepSleepController**: Specialized controller for deep sleep functionality
   - Manages deep sleep timers and timeout handling
   - Coordinates with network standby mode settings
   - Handles graceful deep sleep transitions
   - Provides notification callbacks for deep sleep events

3. **ThermalController**: Temperature monitoring and thermal protection
   - Continuous temperature monitoring through MFR (Manufacturer) HAL
   - Automatic thermal state management (NORMAL, HIGH, CRITICAL)
   - Emergency thermal protection with automatic power state changes
   - RFC (Remote Feature Control) integration for thermal thresholds

4. **RebootController**: System reboot and maintenance operations
   - Graceful system restart functionality
   - Maintenance reboot scheduling
   - Integration with system services for coordinated shutdowns

#### Support Layer
- **AckController**: Pre-change acknowledgment system for power state transitions
- **Settings**: Configuration management for power states and plugin behavior
- **PowerUtils**: Utility functions for power state conversions and logging

### Hardware Abstraction Layer (HAL)

#### Platform Interface (`hal/PowerImpl.h`)
- **IPlatform**: Abstract interface for hardware-specific power operations
- **PowerImpl**: Default platform implementation
- Provides abstraction for:
  - Power state get/set operations
  - Wakeup source configuration
  - Deep sleep timer management
  - Platform-specific initialization/termination

#### External HAL Dependencies
- **Power HAL**: Hardware power state management
- **DeepSleep HAL**: Low-power mode controls
- **MFR HAL**: Manufacturer-specific thermal monitoring
- **IARM Bus**: Inter-Application Resource Manager communication
- **Device Settings**: Platform configuration interface
- **RFC API**: Remote feature configuration

## Data Flow Architecture

### Power State Change Flow
1. **Request Initiation**: Client requests power state change through IPowerManager interface
2. **Pre-Change Notification**: AckController notifies registered clients for acknowledgment
3. **Acknowledgment Collection**: System waits for client acknowledgments within timeout
4. **Validation**: PowerController validates target power state and prerequisites
5. **HAL Execution**: Platform-specific power state change through IPlatform interface
6. **State Persistence**: Settings controller updates configuration persistence
7. **Post-Change Notification**: Broadcast power state change notifications to all subscribers

### Thermal Management Flow
1. **Continuous Monitoring**: ThermalController polls temperature through MFR HAL
2. **Threshold Evaluation**: Compare current temperature against configured limits
3. **State Determination**: Calculate thermal state (NORMAL/HIGH/CRITICAL)
4. **Emergency Action**: Automatic power state changes for critical thermal conditions
5. **Event Notification**: Broadcast thermal state changes to registered clients

### Deep Sleep Management Flow
1. **Timer Configuration**: Configure deep sleep timeout through DeepSleepController
2. **Network Integration**: Coordinate with network standby mode settings
3. **Sleep Preparation**: Prepare system for low-power state
4. **HAL Integration**: Execute platform-specific deep sleep through DeepSleep HAL
5. **Wakeup Handling**: Process wakeup events and restore system state

## Plugin Framework Integration

### Thunder Framework Interfaces
- **PluginHost::IPlugin**: Core plugin lifecycle management
- **PluginHost::IDispatcher**: JSON-RPC method dispatch
- **Exchange::IPowerManager**: Primary functional interface

### Service Registration
- Automatic service discovery and registration with Thunder framework
- JSON-RPC API exposure for external client communication
- Event-driven notification system for power state changes

### Client Communication
- **JSON-RPC API**: RESTful API for power management operations
- **Event Notifications**: Asynchronous notifications for state changes
- **Client Registration**: Support for multiple client subscriptions

## Dependencies and Interfaces

### Internal Dependencies
- WPEFramework/Thunder core libraries
- Plugin framework infrastructure
- JSON-RPC communication layer

### External System Integration
- **IARM Bus**: System-wide inter-process communication
- **RFC System**: Remote configuration management
- **Device Settings**: Platform configuration interface
- **System Services**: Integration with other system components

### HAL Interface Requirements
- Platform-specific power management HAL
- Thermal monitoring capabilities (MFR HAL)
- Deep sleep hardware support (DeepSleep HAL)
- Network interface for standby mode coordination

## Error Handling and Reliability

### Fault Tolerance
- Comprehensive error handling at each controller level
- Graceful degradation for missing HAL components
- Automatic recovery mechanisms for thermal emergencies
- Robust timeout handling for client acknowledgments

### State Consistency
- Atomic power state transitions with rollback capability
- Persistent state storage for recovery across reboots
- Validation of power state prerequisites before transitions
- Synchronization mechanisms for concurrent operations

### Logging and Diagnostics
- Comprehensive logging through UtilsLogging interface
- Error reporting and diagnostic information
- Performance monitoring and metrics collection
- Debug support for development and troubleshooting

This architecture ensures a robust, scalable, and maintainable power management system that integrates seamlessly with the WPEFramework ecosystem while providing comprehensive power control capabilities for embedded devices.