#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_core.h>
#include <string.h>

/* Simplified network info command since WiFi specific APIs aren't available */
static int cmd_network_info(const struct shell *sh, size_t argc, char *argv[])
{
    struct net_if *iface = net_if_get_default();
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No network interface found\n");
        return -ENODEV;
    }

    shell_fprintf(sh, SHELL_NORMAL, "Network interface: %p\n", iface);
    shell_fprintf(sh, SHELL_NORMAL, "Status: %s\n", 
                  net_if_is_up(iface) ? "UP" : "DOWN");
    
    /* Display IPv4 address if available */
    struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
    if (ipv4) {
        char addr_str[NET_IPV4_ADDR_LEN];
        
        if (net_ipv4_is_addr_unspecified(&ipv4->unicast[0].ipv4.address.in_addr)) {
            shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: Not assigned\n");
        } else {
            net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr, 
                         addr_str, sizeof(addr_str));
            shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: %s\n", addr_str);
        }
    }

    return 0;
}

/* Network interface up/down command */
static int cmd_network_state(const struct shell *sh, size_t argc, char *argv[])
{
    bool up;
    struct net_if *iface = net_if_get_default();
    
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No network interface found\n");
        return -ENODEV;
    }
    
    if (argc < 2) {
        shell_fprintf(sh, SHELL_ERROR, "Usage: net state <up|down>\n");
        return -EINVAL;
    }

    if (strcmp(argv[1], "up") == 0) {
        up = true;
    } else if (strcmp(argv[1], "down") == 0) {
        up = false;
    } else {
        shell_fprintf(sh, SHELL_ERROR, "Invalid argument: %s\n", argv[1]);
        return -EINVAL;
    }

    if (up) {
        net_if_up(iface);
        shell_fprintf(sh, SHELL_NORMAL, "Network interface UP\n");
    } else {
        net_if_down(iface);
        shell_fprintf(sh, SHELL_NORMAL, "Network interface DOWN\n");
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(net_cmds,
    SHELL_CMD(info, NULL, "Show network interface information", cmd_network_info),
    SHELL_CMD(state, NULL, "Set network interface state: net state <up|down>", cmd_network_state),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(net, &net_cmds, "Network commands", NULL);
