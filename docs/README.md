# Documentation Index

## Getting Started

| Document | Description |
|---|---|
| [Quickstart](./guides/quickstart.md) | Get up and running in 5 minutes |
| [Deployment Guide](./guides/deployment.md) | Configuration per appliance type |
| [Root README](../README.md) | Project overview and YAML configuration |

## Architecture

| Document | Description |
|---|---|
| [Overview](./architecture/overview.md) | C4 model: system context, container, and component diagrams |
| [Startup Sequence](./architecture/startup-sequence.md) | 8-phase startup HSM sequence diagram |
| [Data Flow](./architecture/data-flow.md) | Read path, write path, and discovery flow diagrams |
| [Roadmap](./architecture/roadmap.md) | Future features and architectural improvements |
| [Module Descriptions](./module_descriptions/) | Lightweight summary for each module |

## Specifications

Detailed behavioral contracts for each module. Each spec covers purpose, interface, behavior, and notes.

### Startup

| Spec | Source |
|---|---|
| [GeappliancesBridge](./spec/geappliances_bridge.md) | `geappliances_bridge.h` / `.cpp` |
| [Startup HSM](./spec/geappliances_bridge_startup_hsm.md) | `geappliances_bridge_startup_hsm.h` / `.cpp` |
| [Autodiscovery Manager](./spec/autodiscovery_manager.md) | `autodiscovery_manager.h` / `.cpp` |
| [Device Identity Manager](./spec/device_identity_manager.md) | `device_identity_manager.h` / `.cpp` |
| [Feature Bit Manager](./spec/feature_bit_manager.md) | `feature_bit_manager.h` / `.cpp` |
| [IBridgeServices](./spec/i_bridge_services.md) | `i_bridge_services.h` |

### Data Bridges

| Spec | Source |
|---|---|
| [Polling Bridge](./spec/erd_bridge_poll.md) | `erd_bridge_poll.h` / `.cpp` |
| [Subscription Bridge](./spec/erd_bridge_subscribe.md) | `erd_bridge_subscribe.h` / `.cpp` |
| [Write Bridge](./spec/erd_write_bridge.md) | `erd_write_bridge.h` / `.cpp` |
| [Bridge Common](./spec/erd_bridge_common.md) | `erd_bridge_common.h` |
| [Bridge Mode](./spec/bridge_mode.md) | `bridge_mode.h` |
| [Poll List Builder](./spec/erd_poll_list_builder.md) | `erd_poll_list_builder.h` / `.cpp` |
| [Polling Bridge Behavior](./spec/polling_bridge_behavior.md) | Behavioral requirements |
| [Bridge Init](./spec/geappliances_bridge_bridge_init.md) | `geappliances_bridge_bridge_init.cpp` |

### Data Layer

| Spec | Source |
|---|---|
| [ERD Cache](./spec/erd_cache.md) | `erd_cache.h` / `.cpp` |
| [ERD Registry](./spec/erd_registry.md) | `erd_registry.h` / `.cpp` |
| [MQTT Publisher](./spec/erd_cache_mqtt_publisher.md) | `erd_cache_mqtt_publisher.h` / `.cpp` |

### Transport Adapters

| Spec | Source |
|---|---|
| [MQTT Client Adapter](./spec/esphome_mqtt_client_adapter.md) | `esphome_mqtt_client_adapter.h` / `.cpp` |
| [UART Adapter](./spec/esphome_uart_adapter.md) | `esphome_uart_adapter.h` / `.cpp` |
| [GEA2 Adapter](./spec/gea2_erd_client_adapter.md) | `gea2_erd_client_adapter.h` / `.cpp` |
| [Time Source](./spec/esphome_time_source.md) | `esphome_time_source.h` / `.cpp` |
| [i_mqtt_client Interface](./spec/i_mqtt_client.md) | `i_mqtt_client.h` |

### Home Assistant Discovery

| Spec | Source |
|---|---|
| [HA Discovery Manager](./spec/ha_discovery_manager.md) | `ha_discovery_manager.h` / `.cpp` |
| [HA Discovery Cleanup](./spec/ha_discovery_cleanup.md) | `ha_discovery_cleanup.h` / `.cpp` |
| [Discovery Refresh Button](./spec/discovery_refresh_button.md) | ESPHome button component |
| [MQTT Data Publishing](./spec/mqtt_data_publishing.md) | Behavioral requirements |
| [Bridge Constants](./spec/geappliances_bridge_constants.md) | `geappliances_bridge_constants.h` |

## Guides

| Guide | Description |
|---|---|
| [Quickstart](./guides/quickstart.md) | Get up and running in 5 minutes |
| [Deployment](./guides/deployment.md) | Configuration per appliance type |
| [Troubleshooting](./guides/troubleshooting.md) | Common issues and diagnostic flow |
| [Development](./guides/development.md) | Build, test, debug workflow |
| [Pipeline](./guides/pipeline.md) | HA discovery pipeline end-to-end |

## Reference

| Reference | Description |
|---|---|
| [YAML Configuration](./reference/yaml-config.md) | All component config options |
| [MQTT Topics](./reference/mqtt-topics.md) | Topic schema, payloads, QoS |
| [ERD Protocol](./reference/erd-protocol.md) | GEA2/GEA3 protocol overview |
| [Interfaces](./reference/interfaces.md) | Key C interface documentation |
| [Hardware](../HARDWARE.md) | Hardware configuration and pin mapping |

## Quality

| Document | Description |
|---|---|
| [Style Guide](./style-guide.md) | Formatting conventions, spec template, review checklist |
| [Review Checklist](./review-checklist.md) | Pre-commit quality checklist |

## Process
How work moves through the repo, the shared vocabulary, and the decisions behind it.

| Document | Description |
|---|---|
| [Software Factory](./factory.md) | Feature/bug lifecycle: triage → grill → spec → tickets → implement → review → device verify → PR |
| [Glossary](./GLOSSARY.md) | Shared language: protocol, data path, discovery, and factory terms |
| [Architecture Decision Records](./adr/) | Hard-to-reverse decisions: context, decision, consequences |
