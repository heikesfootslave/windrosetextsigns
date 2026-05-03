local UEHelpers = require("UEHelpers")

print("[WindroseTextSignsAssetLoader] Mod loaded\n")

local loaded = false

local assets = {
    {
        PackageName = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text",
        AssetName = "DA_BI_Utilities_Lables_Wooden_Text",
    },
    {
        PackageName = "/Game/UI/HUD/Building/Icons/BuildingBits/T_PlaqueT02_None",
        AssetName = "T_PlaqueT02_None",
    },
    {
        PackageName = "/Game/Gameplay/Building/Actors/BP_BuildingBlock_WallPlaqueT02_03",
        AssetName = "BP_BuildingBlock_WallPlaqueT02_03",
    },
}

local function FindAssetRegistryHelpers()
    return StaticFindObject("/Script/AssetRegistry.Default__AssetRegistryHelpers")
end

local function LoadTextSignAssets(reason)
    if loaded then
        return true
    end

    local AssetRegistryHelpers = FindAssetRegistryHelpers()
    if AssetRegistryHelpers == nil then
        print("[WindroseTextSignsAssetLoader] AssetRegistryHelpers unavailable during " .. reason .. "\n")
        return false
    end

    local successCount = 0
    for _, asset in ipairs(assets) do
        local AssetData = {
            ["PackageName"] = UEHelpers.FindOrAddFName(asset.PackageName),
            ["AssetName"] = UEHelpers.FindOrAddFName(asset.AssetName),
        }

        local resolvedAsset = AssetRegistryHelpers:GetAsset(AssetData)
        if resolvedAsset ~= nil then
            successCount = successCount + 1
            print("[WindroseTextSignsAssetLoader] Loaded " .. asset.PackageName .. " during " .. reason .. "\n")
        else
            print("[WindroseTextSignsAssetLoader] GetAsset returned nil for " .. asset.PackageName .. " during " .. reason .. "\n")
        end
    end

    loaded = successCount > 0
    print("[WindroseTextSignsAssetLoader] Load pass complete during " .. reason .. ": " .. tostring(successCount) .. "/" .. tostring(#assets) .. "\n")
    return loaded
end

LoadTextSignAssets("lua startup")

RegisterBeginPlayPostHook(function(ContextParam)
    LoadTextSignAssets("BeginPlay")
end)
