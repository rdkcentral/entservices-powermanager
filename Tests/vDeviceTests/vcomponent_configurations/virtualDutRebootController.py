#!/usr/bin/env python3
#* ******************************************************************************
#*  If not stated otherwise in this file or this component's LICENSE
#*  file the following copyright and licenses apply:
#*
#*  Copyright 2026 RDK Management
#*
#*  Licensed under the Apache License, Version 2.0 (the License);
#*  you may not use this file except in compliance with the License.
#*  You may obtain a copy of the License at
#*
#*  http://www.apache.org/licenses/LICENSE-2.0
#*
#*  Unless required by applicable law or agreed to in writing, software
#*  distributed under the License is distributed on an AS IS BASIS,
#*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#*  See the License for the specific language governing permissions and
#*  limitations under the License.
#*
#* ******************************************************************************

import os
import sys

dir_path = os.path.dirname(os.path.realpath(__file__))
sys.path.append(dir_path)
sys.path.append(os.path.join(dir_path, "../../raft/"))

from raft.framework.core.logModule import logModule
from abstractDutRebootController import DutRebootInterface
from raft.framework.core.utPlaneController import utPlaneController

class VirtualDutRebootController(DutRebootInterface):
    """
    Virtual DUT Reboot Controller for vDevice/vComponent.
    Sends boot reason messages to the virtual device using utPlaneController and YAML commands.
    """

    def __init__(self, logger: logModule, session, control_port: int = 8080):
        """
        Initialize the VirtualDutRebootController for vDevice communication.
        
        Args:
            logger (logModule): Logger module instance for logging operations.
            session: SSH session to the device
            control_port (int, optional): Port number for ut-controller communication. Defaults to 8080.
        """
        super().__init__(logger)
        self.control_port = control_port
        self.controller = utPlaneController(session, port=control_port, log=logger)
        self._log.info(f"VirtualDutRebootController initialized with control port {control_port}")

    def triggerReboot(self, boot_reason: str) -> bool:
        """
        Trigger a device reboot with the specified boot reason via ut-controller.
        
        Args:
            boot_reason (str): The boot reason to set (e.g., "WATCHDOG", "MAINTENANCE_REBOOT", etc.)
        
        Returns:
            bool: True if reboot command sent successfully, False otherwise
        """
        yaml_content = self._buildBootReasonYaml(boot_reason)
        
        try:
            self.controller.sendMessage(yaml_content)
            self._log.info(f"Sent boot reason '{boot_reason}' to virtual device via ut-controller")
            return True
        except Exception as e:
            self._log.error(f"Failed to send boot reason to virtual device: {e}")
            return False

    def _buildBootReasonYaml(self, boot_reason: str) -> str:
        """
        Build YAML content for boot reason command.
        
        Args:
            boot_reason (str): The boot reason to set
        
        Returns:
            str: YAML formatted command string
        """
        # Map of supported boot reasons
        supported_reasons = [
            "WATCHDOG",
            "MAINTENANCE_REBOOT",
            "THERMAL_RESET",
            "WARM_RESET",
            "COLD_BOOT",
            "STR_AUTH_FAILURE"
        ]
        
        # Use the provided boot reason if valid, otherwise default to ERROR_UNKNOWN
        reason = boot_reason if boot_reason in supported_reasons else "ERROR_UNKNOWN"
        
        yaml_content = (
            "boot:\n"
            "  command: bootReason\n"
            "  params:\n"
            f"    reason: \"{reason}\"\n"
        )
        
        return yaml_content
