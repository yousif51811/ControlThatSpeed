#include "pumpkin_api.h"

static const char* authors[] = {"Zerorifter"};

pumpkin_metadata_t get_meta(void) {
    return (pumpkin_metadata_t) {
        .name = "ControlThatSpeed",
        .version = "0.1.0",
        .authors = authors,
        .authors_count = sizeof(authors) / sizeof(authors[0]),
        .description = "A speed control plugin",
        .dependencies_count = 0,
        .permissions_count = 0
    };
}

static void apply_attribute(pumpkin_plugin_world_attribute_t attribute,
        pumpkin_plugin_player_borrow_player_t player,
        float value) {
    (void)player;
    (void)value;
    auto entity = pumpkin_plugin_player_method_player_as_entity(player);
    pumpkin_plugin_world_own_living_entity_t living;
    if (pumpkin_plugin_world_method_entity_as_living(
            pumpkin_plugin_world_borrow_entity(entity), &living)) {
        pumpkin_plugin_world_method_living_entity_set_attribute_base(
            pumpkin_plugin_world_borrow_living_entity(living),
            attribute,
            value);
        pumpkin_plugin_world_living_entity_drop_own(living);
    }
    pumpkin_plugin_world_entity_drop_own(entity);
}

// Put the implementation for each speed type in its own method.
static void apply_fly_speed(pumpkin_plugin_player_borrow_player_t player, float multiplier) {
    // The API setter takes the raw ability speed; vanilla flight speed is 0.05.
    pumpkin_plugin_player_method_player_set_fly_speed(player, 0.05f * multiplier);
}

static void apply_sneak_speed(pumpkin_plugin_player_borrow_player_t player, float multiplier) {
    apply_attribute(PUMPKIN_PLUGIN_ATTRIBUTES_ATTRIBUTE_SNEAKING_SPEED, player, 0.3f * multiplier);
}

static void apply_mine_speed(pumpkin_plugin_player_borrow_player_t player, float multiplier) {
    apply_attribute(PUMPKIN_PLUGIN_ATTRIBUTES_ATTRIBUTE_BLOCK_BREAK_SPEED, player, 1.0f * multiplier);
}

static void apply_submerged_mine_speed(pumpkin_plugin_player_borrow_player_t player, float multiplier) {
    apply_attribute(PUMPKIN_PLUGIN_ATTRIBUTES_ATTRIBUTE_SUBMERGED_MINING_SPEED, player, 0.2f * multiplier);
}

static void apply_movement_speed(pumpkin_plugin_player_borrow_player_t player, float multiplier) {
    apply_attribute(PUMPKIN_PLUGIN_ATTRIBUTES_ATTRIBUTE_MOVEMENT_SPEED, player, 0.1f * multiplier);
}

typedef void (*speed_apply_fn)(pumpkin_plugin_player_borrow_player_t, float);

static bool handle_speed_command(
    plugin_own_command_sender_t sender,
    plugin_own_consumed_args_t args,
    int32_t* ret,
    speed_apply_fn apply,
    bool use_selector
) {
    pumpkin_plugin_command_borrow_consumed_args_t borrowed_args =
        pumpkin_plugin_command_borrow_consumed_args(args);
    plugin_string_t multiplier_key;
    plugin_string_set(&multiplier_key, "multiplier");
    pumpkin_plugin_command_arg_t multiplier_arg = {};
    pumpkin_plugin_command_method_consumed_args_get_value(
        borrowed_args, &multiplier_key, &multiplier_arg);

    if (multiplier_arg.tag != PUMPKIN_PLUGIN_COMMAND_ARG_NUM ||
        multiplier_arg.val.num.is_err ||
        multiplier_arg.val.num.val.ok.tag != PUMPKIN_PLUGIN_COMMAND_NUMBER_FLOAT64) {
        *ret = 0;
        return true;
    }
    const float multiplier = (float)multiplier_arg.val.num.val.ok.val.float64;

    if (use_selector) {
        plugin_string_t players_key;
        plugin_string_set(&players_key, "players");
        pumpkin_plugin_command_arg_t players_arg = {};
        pumpkin_plugin_command_method_consumed_args_get_value(
            borrowed_args, &players_key, &players_arg);
        if (players_arg.tag != PUMPKIN_PLUGIN_COMMAND_ARG_PLAYERS) {
            *ret = 0;
            return true;
        }

        *ret = (int32_t)players_arg.val.players.len;
        for (size_t i = 0; i < players_arg.val.players.len; ++i) {
            apply(pumpkin_plugin_player_borrow_player(players_arg.val.players.ptr[i]), multiplier);
        }
        return true;
    }

    pumpkin_plugin_player_own_player_t own_player;
    if (!pumpkin_plugin_command_method_command_sender_as_player(
            pumpkin_plugin_command_borrow_command_sender(sender), &own_player)) {
        *ret = 0;
        return true;
    }
    apply(pumpkin_plugin_player_borrow_player(own_player), multiplier);
    pumpkin_plugin_player_player_drop_own(own_player);
    *ret = 1;
    return true;
}

#define DEFINE_SPEED_HANDLERS(name, apply_method) \
    static bool handle_##name##_self(plugin_own_command_sender_t sender, \
        plugin_own_server_instance_t server, plugin_own_consumed_args_t args, \
        int32_t* ret, plugin_command_error_t* err) { \
        (void)server; (void)err; \
        return handle_speed_command(sender, args, ret, apply_method, false); \
    } \
    static bool handle_##name##_players(plugin_own_command_sender_t sender, \
        plugin_own_server_instance_t server, plugin_own_consumed_args_t args, \
        int32_t* ret, plugin_command_error_t* err) { \
        (void)sender; (void)server; (void)err; \
        return handle_speed_command(sender, args, ret, apply_method, true); \
    }

DEFINE_SPEED_HANDLERS(fly_speed, apply_fly_speed)
DEFINE_SPEED_HANDLERS(sneak_speed, apply_sneak_speed)
DEFINE_SPEED_HANDLERS(mine_speed, apply_mine_speed)
DEFINE_SPEED_HANDLERS(submerged_mine_speed, apply_submerged_mine_speed)
DEFINE_SPEED_HANDLERS(movement_speed, apply_movement_speed)

static void add_speed_type(
    pumpkin_plugin_command_borrow_command_t command,
    const char* type_name,
    uint32_t self_handler_id,
    uint32_t players_handler_id
) {
    plugin_string_t literal_name;
    plugin_string_set(&literal_name, type_name);
    pumpkin_plugin_command_own_command_node_t type_node =
        pumpkin_plugin_command_static_command_node_literal(&literal_name);

    pumpkin_plugin_command_argument_type_t number_type = {};
    number_type.tag = PUMPKIN_PLUGIN_COMMAND_ARGUMENT_TYPE_DOUBLE;
    plugin_string_t multiplier_name;
    plugin_string_set(&multiplier_name, "multiplier");
    pumpkin_plugin_command_own_command_node_t multiplier_node =
        pumpkin_plugin_command_static_command_node_argument(&multiplier_name, &number_type);

    pumpkin_plugin_command_argument_type_t players_type = {};
    players_type.tag = PUMPKIN_PLUGIN_COMMAND_ARGUMENT_TYPE_PLAYERS;
    plugin_string_t players_name;
    plugin_string_set(&players_name, "players");
    pumpkin_plugin_command_own_command_node_t players_node =
        pumpkin_plugin_command_static_command_node_argument(&players_name, &players_type);

    pumpkin_plugin_command_method_command_node_execute_with_handler_id(
        pumpkin_plugin_command_borrow_command_node(multiplier_node), self_handler_id);
    pumpkin_plugin_command_method_command_node_execute_with_handler_id(
        pumpkin_plugin_command_borrow_command_node(players_node), players_handler_id);

    // /speed <type> <multiplier> [players]
    pumpkin_plugin_command_method_command_node_then(
        pumpkin_plugin_command_borrow_command_node(multiplier_node), players_node);
    pumpkin_plugin_command_method_command_node_then(
        pumpkin_plugin_command_borrow_command_node(type_node), multiplier_node);
    pumpkin_plugin_command_method_command_then(command, type_node);
}

static void register_speed_alias(
    pumpkin_plugin_context_borrow_context_t context,
    const char* const* names,
    size_t name_count,
    const char* description,
    uint32_t self_handler_id,
    uint32_t players_handler_id
) {
    plugin_string_t name_storage[name_count];
    for (size_t i = 0; i < name_count; ++i) {
        plugin_string_set(&name_storage[i], names[i]);
    }
    plugin_list_string_t command_names = { .ptr = name_storage, .len = name_count };
    plugin_string_t command_description;
    plugin_string_set(&command_description, description);
    pumpkin_plugin_command_own_command_t command =
        pumpkin_plugin_command_constructor_command(&command_names, &command_description);

    pumpkin_plugin_command_argument_type_t number_type = {};
    number_type.tag = PUMPKIN_PLUGIN_COMMAND_ARGUMENT_TYPE_DOUBLE;
    plugin_string_t multiplier_name;
    plugin_string_set(&multiplier_name, "multiplier");
    pumpkin_plugin_command_own_command_node_t multiplier_node =
        pumpkin_plugin_command_static_command_node_argument(&multiplier_name, &number_type);

    pumpkin_plugin_command_argument_type_t players_type = {};
    players_type.tag = PUMPKIN_PLUGIN_COMMAND_ARGUMENT_TYPE_PLAYERS;
    plugin_string_t players_name;
    plugin_string_set(&players_name, "players");
    pumpkin_plugin_command_own_command_node_t players_node =
        pumpkin_plugin_command_static_command_node_argument(&players_name, &players_type);

    pumpkin_plugin_command_method_command_node_execute_with_handler_id(
        pumpkin_plugin_command_borrow_command_node(multiplier_node), self_handler_id);
    pumpkin_plugin_command_method_command_node_execute_with_handler_id(
        pumpkin_plugin_command_borrow_command_node(players_node), players_handler_id);
    pumpkin_plugin_command_method_command_node_then(
        pumpkin_plugin_command_borrow_command_node(multiplier_node), players_node);
    pumpkin_plugin_command_method_command_then(
        pumpkin_plugin_command_borrow_command(command), multiplier_node);

    plugin_string_t permission;
    plugin_string_set(&permission, "ControlThatSpeed:command.speed");
    pumpkin_plugin_context_method_context_register_command(context, command, &permission);
}

static void register_speed_permission(pumpkin_plugin_context_borrow_context_t context) {
    pumpkin_plugin_permission_permission_t permission = {};
    plugin_string_set(&permission.node, "ControlThatSpeed:command.speed");
    plugin_string_set(&permission.description, "Use speed commands");
    permission.default_.tag = PUMPKIN_PLUGIN_PERMISSION_PERMISSION_DEFAULT_OP;
    permission.default_.val.op = PUMPKIN_PLUGIN_PERMISSION_PERMISSION_LEVEL_TWO;

    plugin_string_t error = {};
    pumpkin_plugin_context_method_context_register_permission(context, &permission, &error);
}

void on_load(plugin_own_context_t ctx) {
    pumpkin_plugin_context_borrow_context_t context =
        pumpkin_plugin_context_borrow_context(ctx);

    register_speed_permission(context);

    plugin_string_t command_name;
    plugin_string_set(&command_name, "speed");
    plugin_list_string_t command_names = {
        .ptr = &command_name,
        .len = 1
    };
    plugin_string_t description;
    plugin_string_set(&description, "Set a player's speed multiplier");
    pumpkin_plugin_command_own_command_t command =
        pumpkin_plugin_command_constructor_command(&command_names, &description);
    pumpkin_plugin_command_borrow_command_t borrowed_command =
        pumpkin_plugin_command_borrow_command(command);

    add_speed_type(borrowed_command, "fly",
        (uint32_t)(uintptr_t)handle_fly_speed_self,
        (uint32_t)(uintptr_t)handle_fly_speed_players);
    add_speed_type(borrowed_command, "sneak",
        (uint32_t)(uintptr_t)handle_sneak_speed_self,
        (uint32_t)(uintptr_t)handle_sneak_speed_players);
    add_speed_type(borrowed_command, "mine",
        (uint32_t)(uintptr_t)handle_mine_speed_self,
        (uint32_t)(uintptr_t)handle_mine_speed_players);
    add_speed_type(borrowed_command, "submerged_mine",
        (uint32_t)(uintptr_t)handle_submerged_mine_speed_self,
        (uint32_t)(uintptr_t)handle_submerged_mine_speed_players);
    add_speed_type(borrowed_command, "movement",
        (uint32_t)(uintptr_t)handle_movement_speed_self,
        (uint32_t)(uintptr_t)handle_movement_speed_players);

    plugin_string_t permission;
    plugin_string_set(&permission, "ControlThatSpeed:command.speed");
    pumpkin_plugin_context_method_context_register_command(context, command, &permission);

    static const char* fly_names[] = {"flyspeed", "fs"};
    static const char* sneak_names[] = {"sneakspeed", "ss"};
    static const char* mine_names[] = {"minespeed", "ms"};
    static const char* submerged_mine_names[] = {"submergedminespeed", "sms"};
    static const char* movement_names[] = {"movementspeed", "movespeed"};

    register_speed_alias(context, fly_names, 2, "Set flight speed",
        (uint32_t)(uintptr_t)handle_fly_speed_self,
        (uint32_t)(uintptr_t)handle_fly_speed_players);
    register_speed_alias(context, sneak_names, 2, "Set sneaking speed",
        (uint32_t)(uintptr_t)handle_sneak_speed_self,
        (uint32_t)(uintptr_t)handle_sneak_speed_players);
    register_speed_alias(context, mine_names, 2, "Set mining speed",
        (uint32_t)(uintptr_t)handle_mine_speed_self,
        (uint32_t)(uintptr_t)handle_mine_speed_players);
    register_speed_alias(context, submerged_mine_names, 2, "Set submerged mining speed",
        (uint32_t)(uintptr_t)handle_submerged_mine_speed_self,
        (uint32_t)(uintptr_t)handle_submerged_mine_speed_players);
    register_speed_alias(context, movement_names, 2, "Set movement speed",
        (uint32_t)(uintptr_t)handle_movement_speed_self,
        (uint32_t)(uintptr_t)handle_movement_speed_players);

}

REGISTER_PUMPKIN_PLUGIN(((pumpkin_plugin_t){
    .get_metadata = get_meta,
    .on_load = on_load
}))
