// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "battle_material.hpp"
#include "battle_mesh.hpp"
#include "MatrixMap.hpp"

namespace tests {
// Synthetic FO_GIVE_BOT configuration and one body mesh; no rotor, weapon or
// stream artwork. The caller supplies BattleMaterialProfile plus an explicit
// unavailable-icon-target session. All cache/configuration data is synthetic.
inline CBlockPar &install_battle_transport(int score = 100) {
    MG_CHECK(score > 0 && score <= 130);
    install_battle_texture(OBJECT_PATH_ROBOT_CHASSIS L"2");
    install_battle_texture(OBJECT_PATH_ROBOT_ARMOR L"1");
    const MeshAttachment armor{1, L"armor", 0, 0, 20};
    const MeshAttachment head{1, L"head", 0, 0, 20};
    load_battle_material_mesh(std::span(&armor, 1), OBJECT_PATH_ROBOT_CHASSIS L"2.vo",
                             OBJECT_PATH_ROBOT_CHASSIS L"2");
    load_battle_material_mesh(std::span(&head, 1), OBJECT_PATH_ROBOT_ARMOR L"1.vo",
                             OBJECT_PATH_ROBOT_ARMOR L"1");
    auto *names = g_MatrixData->BlockGetAdd(IF_LABELS_BLOCKPAR)->BlockGetAdd(L"RobotNames");
    names->ParSetAdd(L"Hull1Key", L"Synthetic hull ");
    names->ParSetAdd(L"Chas2Key", L"wheels");
    g_Config.m_RobotShadow = SHADOW_OFF;
    g_Config.m_ObjTexturesGloss = false;
    g_Config.m_ItemChars[ARMOR1_STRUCTURE] = 500;
    constexpr auto texture_name = L"synthetic-transport-body";
    constexpr auto mesh_name = L"synthetic-transport-body.vo";
    install_battle_texture(texture_name);
    load_battle_material_mesh({}, mesh_name, texture_name);
    auto *body = g_MatrixData->BlockGetAdd(L"Models")->BlockGetAdd(L"Flyers")
        ->BlockGetAdd(L"1")->BlockGetAdd(L"Body");
    body->ParSetAdd(L"Model", mesh_name);
    body->ParSetAdd(L"Texture", texture_name);
    auto *order = g_MatrixData->BlockGetAdd(PAR_SOURCE_FLYER_ORDERS)
        ->BlockGetAdd(PAR_SOURCE_FLYER_ORDERS_GIVE_BOT);
    order->ParSetAdd(L"Distance", L"300");
    order->ParSetAdd(L"Height", L"100");
    order->ParSetAdd(L"LandHeight", L"40");
    order->ParSetAdd(L"Flyer", L"1");
    order->ParSetAdd(L"Hitpoint", L"1000");
    auto *bot = order->BlockGetAdd(std::to_wstring(score).c_str());
    bot->ParSetAdd(L"BotChassis", L"2");
    bot->ParSetAdd(L"BotArmor", L"1");
    bot->ParSetAdd(L"BotHead", L"0");
    bot->ParSetAdd(L"BotStrength", L"1");
    bot->ParSetAdd(L"BotHitpoint", L"1500");
    return *order;
}
}
