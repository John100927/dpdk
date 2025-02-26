#include <stdio.h>
#include <stdint.h>
#include <rte_eal.h>
#include <rte_ethdev.h>

int main(int argc, char *argv[]) {
    int ret;
    uint16_t port_id = 0;

    // 初始化 DPDK
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        printf("EAL initialization failed!\n");
        return -1;
    }

    struct rte_eth_dev_info dev_info;
    ret = rte_eth_dev_info_get(port_id, &dev_info);
    if (ret != 0) {
        printf("Failed to get device info for port %d: error %d\n", port_id, ret);
        return -1;
    }

    // 顯示網卡的最大 TX / RX Queue 數量
    printf("Port %d: Max TX Queues: %d\n", port_id, dev_info.max_tx_queues);
    printf("Port %d: Max RX Queues: %d\n", port_id, dev_info.max_rx_queues);

    return 0;
}

