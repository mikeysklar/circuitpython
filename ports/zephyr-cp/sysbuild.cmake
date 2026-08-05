# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_NET_CORE_IMAGE_HCI_IPC)
    # For builds in the nrf5340, we build the netcore image with the controller

    set(NET_APP hci_ipc)
    set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/${NET_APP})

    ExternalZephyrProject_Add(
        APPLICATION ${NET_APP}
        SOURCE_DIR  ${NET_APP_SRC_DIR}
        BOARD       ${SB_CONFIG_NET_CORE_BOARD}
    )

    # Upstream split hci_ipc's single nrf5340_cpunet_iso-bt_ll_sw_split.conf
    # into a base prj.conf plus extra-*.conf overlays. Use EXTRA_CONF_FILE, not
    # CONF_FILE: the latter replaces prj.conf, which would drop the base
    # settings (IPC_SERVICE, MBOX, BT_HCI_RAW) prj.conf now carries.
    #
    # Deliberately NOT layering upstream's extra-iso overlay here: it enables
    # ISO and extended advertising, which _bleio never uses and cpunet's RAM
    # cannot afford (issue #41), and mixing "overlay enables, our file
    # disables" in one merge list produces orphaned-int Kconfig failures.
    # hci_ipc_netcore.conf is the single source: the overlay's controller
    # baseline minus ISO/ext-adv, plus the pre-rebase net-core sizing.
    set(${NET_APP}_EXTRA_CONF_FILE
     ${CMAKE_CURRENT_LIST_DIR}/hci_ipc_netcore.conf
     CACHE INTERNAL ""
    )

    native_simulator_set_child_images(${DEFAULT_IMAGE} ${NET_APP})
    native_simulator_set_final_executable(${DEFAULT_IMAGE})
endif()
