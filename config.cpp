class CfgPatches {
    class BZ_AutoDrive {
        units[] = {
            "BZBusStopSign", "BZ_AutoDrive_TShirt"
        };
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] = {
            "DZ_Data",
            "DZ_Characters",
            "DZ_Characters_Tops",
            "DayZExpansion_AI_Scripts",
            "DayZExpansion_Vehicles_Scripts",
            "DayZExpansion_Quests_Scripts"
        };
    };
};

class CfgMods {
    class BZ_AutoDrive {
        dir = "BZ_AutoDrive";
        picture = "";
        action = "";
        hideName = 1;
        hidePicture = 1;
        name = "BZ_AutoDrive (Vehicle AI Driving Framework)";
        credits = "Sonom4n, Hiperhipo10";
        author = "Sonom4n, Hiperhipo10";
        authorID = "0";
        version = "1.0.0";
        extra = 0;
        type = "mod";

        dependencies[] = { "Game", "World", "Mission" };

        // Keybinds del framework → aparecen en el menu Controles del juego (seccion "BZ AutoDrive"),
        // rebindeables. Reemplaza el HOME hardcodeado + el ControlPanelKey del settings JSON.
        inputs = "BZ_AutoDrive/data/inputs.xml";

        class defs {
            class gameScriptModule {
                value = "";
                files[] = { "BZ_AutoDrive/scripts/3_Game" };
            };
            class worldScriptModule {
                value = "";
                files[] = { "BZ_AutoDrive/scripts/4_World" };
            };
            class missionScriptModule {
                value = "";
                files[] = { "BZ_AutoDrive/scripts/5_Mission" };
            };
        };
    };
};

class CfgVehicles {
    class HouseNoDestruct;
    class TShirt_ColorBase;          // base vanilla (DZ_Characters_Tops)

    class BZBusStopSign : HouseNoDestruct {
        scope = 2;
        model = "\DZ\structures_bliss\signs\sign_citydir_A.p3d";
        vehicleClass = "props";
        hiddenSelections[] = {};
        hiddenSelectionsTextures[] = {};
    };

    // Remera branded (cosmetico del proyecto, traida de @BrigadaZ_Remeras 2026-06-26).
    class BZ_AutoDrive_TShirt : TShirt_ColorBase {
        scope = 2;
        displayName = "Remera Brigada Z";
        descriptionShort = "Remera negra con el logo de la Brigada Z.";
        // ground (inventario) = textura negra vanilla; male/female (puesta) = el logo.
        hiddenSelectionsTextures[] = {
            "DZ\characters\tops\data\tshirt_ground_black_co.paa",
            "BZ_AutoDrive\data\tshirt_brigadaz_co.paa",
            "BZ_AutoDrive\data\tshirt_brigadaz_co.paa"
        };
    };
};
