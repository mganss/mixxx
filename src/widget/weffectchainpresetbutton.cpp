#include "widget/weffectchainpresetbutton.h"

#include <QCheckBox>
#include <QWidgetAction>

#include "effects/presets/effectpresetmanager.h"
#include "widget/effectwidgetutils.h"

WEffectChainPresetButton::WEffectChainPresetButton(QWidget* parent, EffectsManager* pEffectsManager)
        : QPushButton(parent),
          WBaseWidget(this),
          m_iChainNumber(0),
          m_pEffectsManager(pEffectsManager),
          m_pChainPresetManager(pEffectsManager->getChainPresetManager()),
          m_pMenu(make_parented<QMenu>(new QMenu(this))) {
    setMenu(m_pMenu.get());
    connect(m_pChainPresetManager.data(),
            &EffectChainPresetManager::effectChainPresetListUpdated,
            this,
            &WEffectChainPresetButton::populateMenu);
    setFocusPolicy(Qt::NoFocus);
}

void WEffectChainPresetButton::setup(const QDomNode& node, const SkinContext& context) {
    m_iChainNumber = EffectWidgetUtils::getEffectUnitNumberFromNode(node, context);
    m_pChain = EffectWidgetUtils::getEffectChainFromNode(
            node, context, m_pEffectsManager);
    for (const auto& pEffectSlot : m_pChain->getEffectSlots()) {
        connect(pEffectSlot.data(),
                &EffectSlot::effectChanged,
                this,
                &WEffectChainPresetButton::populateMenu);
        connect(pEffectSlot.data(),
                &EffectSlot::parametersChanged,
                this,
                &WEffectChainPresetButton::populateMenu);
    }
    populateMenu();
}

void WEffectChainPresetButton::populateMenu() {
    m_pMenu->clear();

    // Chain preset items
    for (const auto& pChainPreset : m_pChainPresetManager->getPresetsSorted()) {
        m_pMenu->addAction(pChainPreset->name(), this, [this, pChainPreset]() {
            m_pChain->loadChainPreset(pChainPreset);
        });
    }
    m_pMenu->addSeparator();
    m_pMenu->addAction(tr("Save as new preset"), this, [this]() {
        m_pChainPresetManager->savePreset(m_pChain);
    });

    m_pMenu->addSeparator();

    // Effect parameter hiding/showing and saving snapshots
    // TODO: get number of currently visible effect slots from skin
    for (int effectSlotIndex = 0; effectSlotIndex < 3; effectSlotIndex++) {
        auto pEffectSlot = m_pChain->getEffectSlots().at(effectSlotIndex);

        const QString effectSlotNumPrefix(QString::number(effectSlotIndex + 1) + ": ");
        auto pManifest = pEffectSlot->getManifest();
        if (pManifest == nullptr) {
            m_pMenu->addAction(effectSlotNumPrefix + kNoEffectString);
            continue;
        }

        auto pEffectMenu = make_parented<QMenu>(
                effectSlotNumPrefix + pEffectSlot->getManifest()->displayName(),
                m_pMenu);

        const ParameterMap loadedParameters = pEffectSlot->getLoadedParameters();
        const ParameterMap hiddenParameters = pEffectSlot->getHiddenParameters();
        int numTypes = static_cast<int>(EffectManifestParameter::ParameterType::NumTypes);
        for (int parameterTypeId = 0; parameterTypeId < numTypes; ++parameterTypeId) {
            const EffectManifestParameter::ParameterType parameterType =
                    static_cast<EffectManifestParameter::ParameterType>(parameterTypeId);
            for (const auto& pParameter : loadedParameters.value(parameterType)) {
                auto pCheckbox = make_parented<QCheckBox>(pEffectMenu);
                pCheckbox->setChecked(true);
                pCheckbox->setText(pParameter->manifest()->name());
                auto handler = [pCheckbox{pCheckbox.get()}, pEffectSlot, pParameter] {
                    if (pCheckbox->isChecked()) {
                        pEffectSlot->showParameter(pParameter);
                    } else {
                        pEffectSlot->hideParameter(pParameter);
                    }
                };
                connect(pCheckbox.get(), &QCheckBox::stateChanged, this, handler);

                auto pAction = make_parented<QWidgetAction>(pEffectMenu);
                pAction->setDefaultWidget(pCheckbox.get());

                pEffectMenu->addAction(pAction.get());
            }

            for (const auto& pParameter : hiddenParameters.value(parameterType)) {
                auto pCheckbox = make_parented<QCheckBox>(pEffectMenu);
                pCheckbox->setChecked(false);
                pCheckbox->setText(pParameter->manifest()->name());
                auto handler = [pCheckbox{pCheckbox.get()}, pEffectSlot, pParameter] {
                    if (pCheckbox->isChecked()) {
                        pEffectSlot->showParameter(pParameter);
                    } else {
                        pEffectSlot->hideParameter(pParameter);
                    }
                };
                connect(pCheckbox.get(), &QCheckBox::stateChanged, this, handler);

                auto pAction = make_parented<QWidgetAction>(pEffectMenu);
                pAction->setDefaultWidget(pCheckbox.get());

                pEffectMenu->addAction(pAction.get());
            }
        }
        pEffectMenu->addSeparator();

        pEffectMenu->addAction(tr("Save snapshot"), this, [this, pEffectSlot] {
            m_pEffectsManager->getEffectPresetManager()->saveDefaultForEffect(pEffectSlot);
        });
        m_pMenu->addMenu(pEffectMenu);
    }
}
