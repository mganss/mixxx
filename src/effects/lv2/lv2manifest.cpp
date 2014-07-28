#include "effects/lv2/lv2manifest.h"
#include "effects/effectmanifestparameter.h"

LV2Manifest::LV2Manifest(const LilvPlugin* plug,
                         QHash<QString, LilvNode*>& properties)
        : m_isValid(true) {

    m_pLV2plugin = plug;

    // Get and set the ID
    const LilvNode* id = lilv_plugin_get_uri(m_pLV2plugin);
    m_effectManifest.setId(lilv_node_as_string(id));

    // Get and set the name
    LilvNode* info = lilv_plugin_get_name(m_pLV2plugin);
    m_effectManifest.setName(lilv_node_as_string(info));
    lilv_node_free(info);

    // Get and set the author
    info = lilv_plugin_get_author_name(m_pLV2plugin);
    m_effectManifest.setAuthor(lilv_node_as_string(info));
    lilv_node_free(info);

    int numPorts = lilv_plugin_get_num_ports(plug);
    m_minimum = new float[numPorts];
    m_maximum = new float[numPorts];
    m_default = new float[numPorts];
    lilv_plugin_get_port_ranges_float(m_pLV2plugin, m_minimum, m_maximum,
                                      m_default);

    // Counters to determine the type of the plug in
    int inputPorts = 0;
    int outputPorts = 0;

    for (int i = 0; i < numPorts; i++) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);

        if (lilv_port_is_a(m_pLV2plugin, port, properties["audio_port"])) {
            if (lilv_port_is_a(m_pLV2plugin, port, properties["input_port"])) {
                audioPortIndices.append(i);
                inputPorts++;
                info = lilv_port_get_name(m_pLV2plugin, port);
                QString paramName = lilv_node_as_string(info);
                qDebug() << "Input Port name: " << paramName;
            } else if (lilv_port_is_a(m_pLV2plugin, port, properties["output_port"])) {
                audioPortIndices.append(i);
                outputPorts++;
                info = lilv_port_get_name(m_pLV2plugin, port);
                QString paramName = lilv_node_as_string(info);
                qDebug() << "Output Port name: " << paramName;
            }
        }

        if (lilv_port_is_a(m_pLV2plugin, port, properties["control_port"])) {
            controlPortIndices.append(i);
            EffectManifestParameter* param = m_effectManifest.addParameter();

            // Get and set the parameter name
            info = lilv_port_get_name(m_pLV2plugin, port);
            QString paramName = lilv_node_as_string(info);
            param->setName(paramName);
            lilv_node_free(info);

            // Build and set the parameter id from its name
            // Append its index to avoid duplicate ids
            param->setId(paramName.trimmed().toLower().replace(' ', '_').append(i + '0'));
            param->setSemanticHint(EffectManifestParameter::SEMANTIC_UNKNOWN);
            param->setUnitsHint(EffectManifestParameter::UNITS_UNKNOWN);
            param->setDefault(m_default[i]);
            param->setMinimum(m_minimum[i]);
            param->setMaximum(m_maximum[i]);

            // Set the appropriate Hints
            if (lilv_port_has_property(m_pLV2plugin, port, properties["button_port"])) {
                param->setControlHint(EffectManifestParameter::CONTROL_TOGGLE_STEPPING);
            } else if (lilv_port_has_property(m_pLV2plugin, port, properties["enumeration_port"])) {
                buildEnumerationOptions(port, param);
                param->setControlHint(EffectManifestParameter::CONTROL_TOGGLE_STEPPING);
            } else if (lilv_port_has_property(m_pLV2plugin, port, properties["integer_port"])) {
                param->setControlHint(EffectManifestParameter::CONTROL_KNOB_STEPPING);
            } else {
                 param->setControlHint(EffectManifestParameter::CONTROL_KNOB_LINEAR);
            }
        }
    }

    // We only support the case when the input and output samples are stereo
    if (inputPorts != 2 || outputPorts != 2) {
        m_isValid = false;
    }

    // We don't support any features
    LilvNodes* features = lilv_plugin_get_required_features(m_pLV2plugin);
    if (lilv_nodes_size(features) > 0) {
        m_isValid = false;
    }
    lilv_nodes_free(features);
}

LV2Manifest::~LV2Manifest() {
    delete m_minimum;
    delete m_maximum;
    delete m_default;
}

EffectManifest LV2Manifest::getEffectManifest() {
    return m_effectManifest;
}

QList<int> LV2Manifest::getAudioPortIndices() {
    return audioPortIndices;
}

QList<int> LV2Manifest::getControlPortIndices() {
    return controlPortIndices;
}

const LilvPlugin* LV2Manifest::getPlugin() {
    return m_pLV2plugin;
}

bool LV2Manifest::isValid() {
    return m_isValid;
}

void LV2Manifest::buildEnumerationOptions(const LilvPort* port,
                                          EffectManifestParameter* param) {
    LilvScalePoints* options = lilv_port_get_scale_points(m_pLV2plugin, port);
    LILV_FOREACH(scale_points, iterator, options) {
        const LilvScalePoint* option = lilv_scale_points_get(options, iterator);
        const LilvNode* description = lilv_scale_point_get_label(option);
        const LilvNode* value = lilv_scale_point_get_value(option);
        QString strDescription(lilv_node_as_string(description));
        param->appendStep(qMakePair(strDescription, 
		(double)lilv_node_as_float(value)));
    }

    if (options != NULL) {
        lilv_scale_points_free(options);
    }
}
