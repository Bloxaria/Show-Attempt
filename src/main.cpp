#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// ==========================================
// STRUCTURE DES PARAMÈTRES (VARIABLES GLOBALES)
// ==========================================
struct ModSettings {
    bool noclip = false;
    bool showAttempt = false;
    int maxAttempt = 0; // 0 = infini
    bool flashDeadAction = false;
    ccColor3B colorDeadAction = {255, 0, 0}; // Rouge par défaut
    bool colorLegacyAtt = false;
} g_settings;

// Variables d'état de la partie en cours
int g_currentAttempts = 0;
bool g_isWarned = false;
bool g_hasTouchedObstacle = false; // Pour éviter le spam dans un gros bloc
CCLabelBMFont* g_indicatorLabel = nullptr;
CCLabelBMFont* g_statusLabel = nullptr;

// ==========================================
// 1. CREATION ET GESTION DE L'INTERFACE (MENU)
// ==========================================
class MyMenuLayer : public FLAlertLayer {
public:
    CCLayer* m_mainLayer = nullptr;
    std::vector<CCNode*> m_noclipSubNodes;
    std::vector<CCNode*> m_flashSubNodes;

    static MyMenuLayer* create() {
        auto ret = new MyMenuLayer();
        if (ret && ret->init(240, 320, "GJ_square01.png", "Menu du Mod")) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init(float width, float height, const char* bg, const char* title) {
        if (!FLAlertLayer::init(150)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        // Fond du menu (style GD)
        auto background = CCScale9Sprite::create(bg);
        background->setContentSize({width, height});
        background->setPosition(winSize / 2);
        this->addChild(background);

        m_mainLayer = CCLayer::create();
        this->addChild(m_mainLayer);

        // Titre
        auto titleLabel = CCLabelBMFont::create(title, "goldFont.fnt");
        titleLabel->setPosition({winSize.width / 2, winSize.height / 2 + height / 2 - 20});
        titleLabel->setScale(0.8f);
        m_mainLayer->addChild(titleLabel);

        // Bouton Fermer (Croix)
        auto closeBtnSprite = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        auto closeBtn = CCMenuItemSpriteExtra::create(closeBtnSprite, this, menu_selector(MyMenuLayer::onClose));
        auto closeMenu = CCMenu::create(closeBtn, nullptr);
        closeMenu->setPosition({winSize.width / 2 - width / 2 + 15, winSize.height / 2 + height / 2 - 15});
        m_mainLayer->addChild(closeMenu);

        // Menu pour les éléments interactifs
        auto menuItems = CCMenu::create();
        menuItems->setPosition({winSize.width / 2, winSize.height / 2});
        m_mainLayer->addChild(menuItems);

        // --- Option 1: Noclip ---
        auto noclipToggler = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(MyMenuLayer::toggleNoclip), 0.6f);
        noclipToggler->setChecked(g_settings.noclip);
        noclipToggler->setPosition({-100, 60});
        menuItems->addChild(noclipToggler);

        auto noclipText = CCLabelBMFont::create("Noclip", "bigFont.fnt");
        noclipText->setPosition({-60, 60});
        noclipText->setAnchorPoint({0, 0.5f});
        noclipText->setScale(0.4f);
        menuItems->addChild(noclipText);

        this->updateVisibility();
        return true;
    }

    void toggleNoclip(CCObject* sender) {
        g_settings.noclip = !g_settings.noclip;
        if (g_settings.noclip && PlayLayer::get()) {
            g_isWarned = true; // Warn direct si activé en cours de partie
            if (g_statusLabel) g_statusLabel->setString("Warn");
        }
        updateVisibility();
    }

    void updateVisibility() {
        for (auto node : m_noclipSubNodes) node->setVisible(g_settings.noclip);
        for (auto node : m_flashSubNodes) node->setVisible(g_settings.noclip && g_settings.flashDeadAction);
    }

    void onClose(CCObject* sender) {
        this->removeFromParentAndCleanup(true);
    }
};

// INJECTION DU BOUTON "M" DANS L'ÉCRAN DE PAUSE
class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = CCMenu::create();

        // Création du bouton avec le fond classique et le M en or
        auto btnSprite = CCScale9Sprite::create("GJ_button_01.png");
        btnSprite->setContentSize({40, 40});
        
        auto goldM = CCLabelBMFont::create("M", "goldFont.fnt");
        goldM->setPosition(btnSprite->setContentSize() / 2);
        btnSprite->addChild(goldM);

        auto myBtn = CCMenuItemSpriteExtra::create(btnSprite, this, menu_selector(MyPauseLayer::onOpenModMenu));
        menu->addChild(myBtn);
        
        // Alignement tout à droite
        myBtn->setPosition({winSize.width / 2 - 30, 0}); 
    }

    void onOpenModMenu(CCObject* sender) {
        auto menu = MyMenuLayer::create();
        this->addChild(menu, 999);
    }
};


// ==========================================
// 2. LOGIQUE DU JEU (NOCLIP, COMPTEUR, FLASH)
// ==========================================
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontRunActions) {
        if (!PlayLayer::init(level, useReplay, dontRunActions)) return false;

        // Reset des états au point de départ
        g_currentAttempts = 0;
        g_isWarned = g_settings.noclip; // Si déjà coché au départ, c'est direct Warn
        g_hasTouchedObstacle = false;

        // Indicateur discret sur deux lignes séparées (En bas à droite)
        if (g_settings.showAttempt && g_settings.noclip) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            
            // Ligne 1 : Compteur d'essais (Placé plus haut)
            std::string textAtt = "Attempt: 0";
            g_indicatorLabel = CCLabelBMFont::create(textAtt.c_str(), "analyticsFont.fnt");
            g_indicatorLabel->setScale(0.4f);
            g_indicatorLabel->setAnchorPoint({1.0f, 0.0f}); // Aligné à droite
            g_indicatorLabel->setPosition({winSize.width - 10, 25}); 
            this->addChild(g_indicatorLabel, 999);
            
            // Ligne 2 : Statut (Juste en dessous)
            std::string textStatus = g_isWarned ? "Warn" : "Safe";
            g_statusLabel = CCLabelBMFont::create(textStatus.c_str(), "analyticsFont.fnt");
            g_statusLabel->setScale(0.4f);
            g_statusLabel->setAnchorPoint({1.0f, 0.0f}); // Aligné à droite
            g_statusLabel->setPosition({winSize.width - 10, 10}); 
            this->addChild(g_statusLabel, 999);
        }

        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Si le noclip est désactivé, comportement normal de mort
        if (!g_settings.noclip) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        if (player == m_player1 || player == m_player2) {
            g_isWarned = true; // Changement de statut dès qu'on touche un bloc

            if (!g_hasTouchedObstacle) {
                g_currentAttempts++;
                g_hasTouchedObstacle = true;

                // Option Max Attempt : Si on atteint pile la limite stricte réglée, mort immédiate
                if (g_settings.maxAttempt > 0 && g_currentAttempts >= g_settings.maxAttempt) {
                    PlayLayer::destroyPlayer(player, object);
                    return;
                }

                // Mise à jour de l'affichage sur deux lignes distinctes
                if (g_settings.showAttempt) {
                    if (g_indicatorLabel) {
                        g_indicatorLabel->setString(CCString::createWithFormat("Attempt: %d", g_currentAttempts)->getCString());
                    }
                    if (g_statusLabel) {
                        g_statusLabel->setString("Warn");
                    }
                    
                    // Option ColorLegacyAtt : Flash de couleur sur les textes de l'indicateur
                    if (g_settings.colorLegacyAtt && g_settings.flashDeadAction) {
                        if (g_indicatorLabel) g_indicatorLabel->setColor(g_settings.colorDeadAction);
                        if (g_statusLabel) g_statusLabel->setColor(g_settings.colorDeadAction);
                        
                        // Action pour faire revenir au blanc après 0.4s
                        auto resetColor = CCSequence::create(
                            CCDelayTime::create(0.4f),
                            CCTintTo::create(0.2f, 255, 255, 255),
                            nullptr
                        );
                        
                        if (g_indicatorLabel) g_indicatorLabel->runAction(resetColor);
                        if (g_statusLabel) g_statusLabel->runAction(resetColor);
                    }
                }

                // Option FlashDeadAction : Flash plein écran d'un coup dur puis fadeout
                if (g_settings.flashDeadAction) {
                    auto winSize = CCDirector::sharedDirector()->getWinSize();
                    auto flashLayer = CCLayerColor::create({g_settings.colorDeadAction.r, g_settings.colorDeadAction.g, g_settings.colorDeadAction.b, 180});
                    this->addChild(flashLayer, 998);
                    
                    flashLayer->runAction(CCSequence::create(
                        CCFadeOut::create(0.5f),
                        CCRemoveSelf::create(),
                        nullptr
                    ));
                }
            }
            return; // Bloque la mort (Effet Noclip)
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void update(float dt) {
        PlayLayer::update(dt);

        // Reset de la détection dès qu'on sort du bloc pour pouvoir rajouter +1 à la prochaine mort
        if (m_player1 && !m_player1->m_isColliding) {
            g_hasTouchedObstacle = false;
        }
    }

    void levelComplete() {
        // Bloque complètement la validation de la fin si l'état est Warn
        if (g_isWarned) {
            FLAlertLayer::create("Run Invalide", "Noclip utilisé. Progression et récompenses annulées.", "OK")->show();
            this->onQuit(); // Retour direct sans enregistrer les 100%
            return;
        }
        PlayLayer::levelComplete();
    }
};
