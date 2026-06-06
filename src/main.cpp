#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// Variables de session (en cours de partie)
int g_currentAttempts = 0;
bool g_isWarned = false;
bool g_hasTouchedObstacle = false;
CCLabelBMFont* g_indicatorLabel = nullptr;
CCLabelBMFont* g_statusLabel = nullptr;

// ==========================================
// 1. INJECTION DU BOUTON "M" DANS LA PAUSE
// ==========================================
class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = CCMenu::create();

        // Bouton classique avec le M en or
        auto btnSprite = CCScale9Sprite::create("GJ_button_01.png");
        btnSprite->setContentSize({40, 40});
        
        auto goldM = CCLabelBMFont::create("M", "goldFont.fnt");
        goldM->setPosition(btnSprite->setContentSize() / 2);
        btnSprite->addChild(goldM);

        auto myBtn = CCMenuItemSpriteExtra::create(btnSprite, this, menu_selector(MyPauseLayer::onOpenModMenu));
        menu->addChild(myBtn);
        
        // Alignement à droite de l'écran de pause
        myBtn->setPosition({winSize.width / 2 - 30, 0}); 
    }

    void onOpenModMenu(CCObject* sender) {
        // Ouvre automatiquement les paramètres du mod créés par le mod.json
        geode::openSettingsPopup(Mod::get());
    }
};

// ==========================================
// 2. LOGIQUE DES PARAMÈTRES ET DU GAMEPLAY
// ==========================================
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontRunActions) {
        if (!PlayLayer::init(level, useReplay, dontRunActions)) return false;

        // Récupération des options définies dans le menu via l'API Geode correcte
        bool noclipEnabled = Mod::get()->getSettingValue<bool>("noclip");
        bool showAttempt = Mod::get()->getSettingValue<bool>("show-attempt");

        // Reset des états
        g_currentAttempts = 0;
        g_isWarned = noclipEnabled; // Warn direct si lancé avec le noclip coché
        g_hasTouchedObstacle = false;

        // Affichage discret sur deux lignes séparées (En bas à droite)
        if (showAttempt && noclipEnabled) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            
            // Ligne 1 : Compteur
            g_indicatorLabel = CCLabelBMFont::create("Attempt: 0", "analyticsFont.fnt");
            g_indicatorLabel->setScale(0.4f);
            g_indicatorLabel->setAnchorPoint({1.0f, 0.0f});
            g_indicatorLabel->setPosition({winSize.width - 10, 25}); 
            this->addChild(g_indicatorLabel, 999);
            
            // Ligne 2 : Statut (Safe / Warn)
            std::string textStatus = g_isWarned ? "Warn" : "Safe";
            g_statusLabel = CCLabelBMFont::create(textStatus.c_str(), "analyticsFont.fnt");
            g_statusLabel->setScale(0.4f);
            g_statusLabel->setAnchorPoint({1.0f, 0.0f});
            g_statusLabel->setPosition({winSize.width - 10, 10}); 
            this->addChild(g_statusLabel, 999);
        }

        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Récupération dynamique des paramètres de Geode
        bool noclipEnabled = Mod::get()->getSettingValue<bool>("noclip");
        bool showAttempt = Mod::get()->getSettingValue<bool>("show-attempt");
        int maxAttempt = Mod::get()->getSettingValue<int64_t>("max-attempt"); // correction du type int pour Geode
        bool flashDeadAction = Mod::get()->getSettingValue<bool>("flash-dead");
        ccColor3B colorDeadAction = Mod::get()->getSettingValue<ccColor3B>("color-dead");
        bool colorLegacyAtt = Mod::get()->getSettingValue<bool>("color-legacy");

        if (!noclipEnabled) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        if (player == m_player1 || player == m_player2) {
            g_isWarned = true; // Passage immédiat en statut Warn dès qu'on prend un coup

            if (!g_hasTouchedObstacle) {
                g_currentAttempts++;
                g_hasTouchedObstacle = true;

                // Max Attempt : Mort définitive si la limite est atteinte
                if (maxAttempt > 0 && g_currentAttempts >= maxAttempt) {
                    PlayLayer::destroyPlayer(player, object);
                    return;
                }

                // Rafraîchissement de l'affichage à l'écran
                if (showAttempt) {
                    if (g_indicatorLabel) {
                        g_indicatorLabel->setString(CCString::createWithFormat("Attempt: %d", g_currentAttempts)->getCString());
                    }
                    if (g_statusLabel) {
                        g_statusLabel->setString("Warn");
                    }
                    
                    // ColorLegacyAtt : On applique la couleur choisie aux textes au moment de l'impact
                    if (colorLegacyAtt && flashDeadAction) {
                        if (g_indicatorLabel) g_indicatorLabel->setColor(colorDeadAction);
                        if (g_statusLabel) g_statusLabel->setColor(colorDeadAction);
                        
                        auto resetColor = CCSequence::create(
                            CCDelayTime::create(0.4f),
                            CCTintTo::create(0.2f, 255, 255, 255),
                            nullptr
                        );
                        
                        if (g_indicatorLabel) g_indicatorLabel->runAction(resetColor);
                        if (g_statusLabel) g_statusLabel->runAction(resetColor->clone());
                    }
                }

                // FlashDeadAction : Flash complet de tout l'écran avec fondu rapide
                if (flashDeadAction) {
                    auto winSize = CCDirector::sharedDirector()->getWinSize();
                    auto flashLayer = CCLayerColor::create({colorDeadAction.r, colorDeadAction.g, colorDeadAction.b, 180});
                    this->addChild(flashLayer, 998);
                    
                    flashLayer->runAction(CCSequence::create(
                        CCFadeOut::create(0.5f),
                        CCRemoveSelf::create(),
                        nullptr
                    ));
                }
            }
            return; // Bloque le crash (Noclip)
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void update(float dt) {
        PlayLayer::update(dt);
        // On réinitialise la sécurité dès qu'on ne touche plus l'obstacle
        if (m_player1 && !m_player1->m_isColliding) {
            g_hasTouchedObstacle = false;
        }
    }

    void levelComplete() {
        if (g_isWarned) {
            FLAlertLayer::create("Run Invalide", "Noclip utilisé. Progression non sauvegardée.", "OK")->show();
            this->onQuit(); 
            return;
        }
        PlayLayer::levelComplete();
    }
};
