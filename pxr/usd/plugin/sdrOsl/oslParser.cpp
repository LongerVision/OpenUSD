//
// Copyright 2018 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/weakPtr.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/ar/ar.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ndr/debugCodes.h"
#include "pxr/usd/ndr/nodeDiscoveryResult.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdr/shaderMetadataHelpers.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/usd/sdr/shaderProperty.h"
#include "pxr/usd/plugin/sdrOsl/oslParser.h"

#include <tuple>

PXR_NAMESPACE_OPEN_SCOPE

using ShaderMetadataHelpers::IsPropertyAnAssetIdentifier;
using ShaderMetadataHelpers::IsPropertyATerminal;
using ShaderMetadataHelpers::IsTruthy;
using ShaderMetadataHelpers::OptionVecVal;

NDR_REGISTER_PARSER_PLUGIN(SdrOslParserPlugin)

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    ((arraySize, "arraySize"))
    ((pageStr, "page"))
    ((oslPageDelimiter, "."))
    ((vstructMember, "vstructmember"))
    (sdrDefinitionName)

    // Discovery and source type
    ((discoveryType, "oso"))
    ((sourceType, "OSL"))

    ((usdSchemaDefPrefix, "usdSchemaDef_"))
    ((sdrGlobalConfigPrefix, "sdrGlobalConfig_"))
    (sdrDefinitionNameFallbackPrefix)
    
);

const NdrTokenVec& 
SdrOslParserPlugin::GetDiscoveryTypes() const
{
    static const NdrTokenVec _DiscoveryTypes = {_tokens->discoveryType};
    return _DiscoveryTypes;
}

const TfToken& 
SdrOslParserPlugin::GetSourceType() const
{
    return _tokens->sourceType;
}

SdrOslParserPlugin::SdrOslParserPlugin()
{
    // Nothing yet
}

SdrOslParserPlugin::~SdrOslParserPlugin()
{
    // Nothing yet
}

template <class String>
static bool
_ParseFromSourceCode(OSL::OSLQuery* query, const String& sourceCode)
{
#if OSL_LIBRARY_VERSION_CODE < 10701
    TF_WARN("Support for parsing OSL from an in-memory string is only "
            "available in OSL version 1.7.1 or newer.");
    return false;
#else
    return query->open_bytecode(sourceCode);
#endif
}

NdrNodeUniquePtr
SdrOslParserPlugin::Parse(const NdrNodeDiscoveryResult& discoveryResult)
{
    // Each call to `Parse` should have its own reference to an OSL query to
    // prevent multi-threading issues
    OSL::OSLQuery oslQuery;

    bool parseSuccessful = true;

    if (!discoveryResult.uri.empty()) {
        // Attempt to parse the node
        // Since parsing from buffers is only available with OSL > 1.7.1,
        // we explicitly check if we're reading from a file on disk and
        // use the regular open function so that this case still works with
        // older versions.
        if (TfIsFile(discoveryResult.resolvedUri)) {
            parseSuccessful = oslQuery.open(discoveryResult.resolvedUri);
        }
        else {
            std::shared_ptr<const char> buffer;
            std::shared_ptr<ArAsset> asset = ArGetResolver().OpenAsset(
                ArResolvedPath(discoveryResult.resolvedUri));
            if (asset) {
                buffer = asset->GetBuffer();
            }

            if (!buffer) {
                TF_WARN("Could not open the OSL at URI [%s] (%s). An invalid Sdr "
                        "node definition will be created.",
                        discoveryResult.uri.c_str(),
                        discoveryResult.resolvedUri.c_str());
                return NdrParserPlugin::GetInvalidNode(discoveryResult);
            }

            parseSuccessful = _ParseFromSourceCode(
                &oslQuery, OSL::string_view(buffer.get(), asset->GetSize()));
        }

    } else if (!discoveryResult.sourceCode.empty()) {
        parseSuccessful = _ParseFromSourceCode(
            &oslQuery, discoveryResult.sourceCode);
    } else {
        TF_WARN("Invalid NdrNodeDiscoveryResult with identifier %s: both uri "
            "and sourceCode are empty.", discoveryResult.identifier.GetText());
        return NdrParserPlugin::GetInvalidNode(discoveryResult);
    }

    std::string errors = oslQuery.geterror();
    if (!parseSuccessful || !errors.empty()) {
        TF_WARN("Could not parse OSL shader at URI [%s]. An invalid Sdr node "
                "definition will be created. %s%s",
                discoveryResult.uri.c_str(),
                (errors.empty() ? "" : "Errors from OSL parser: "),
                (errors.empty() ? "" : TfStringReplace(errors, "\n", "; ").c_str()));

        return NdrParserPlugin::GetInvalidNode(discoveryResult);
    }

    // The sdrDefinitionFallbackPrefix is found in the node metadata. The 
    // fallbackPrefix is used in getNodeProperties to define the property's 
    // ImplementationName.
    NdrTokenMap metadata = _getNodeMetadata(oslQuery, discoveryResult.metadata);
    std::string fallbackPrefix;
    auto it = metadata.find(_tokens->sdrDefinitionNameFallbackPrefix);
    if (it != metadata.end())
    {
        fallbackPrefix = it->second;
    }

    return NdrNodeUniquePtr(
        new SdrShaderNode(
            discoveryResult.identifier,
            discoveryResult.version,
            discoveryResult.name,
            discoveryResult.family,
            _tokens->sourceType,
            _tokens->sourceType,    // OSL shaders don't declare different types
                                    // so use the same type as the source type
            discoveryResult.resolvedUri,
            discoveryResult.resolvedUri,    // Definitive assertion that the
                                            // implementation is the same asset
                                            // as the definition
            _getNodeProperties(oslQuery, discoveryResult, fallbackPrefix),
            metadata,
            discoveryResult.sourceCode
        )
    );
}

NdrPropertyUniquePtrVec
SdrOslParserPlugin::_getNodeProperties(
    const OSL::OSLQuery &query, 
    const NdrNodeDiscoveryResult& discoveryResult, 
    const std::string& fallbackPrefix) const
{
    NdrPropertyUniquePtrVec properties;
    const size_t nParams = query.nparams();

    for (size_t i = 0; i < nParams; ++i) {
        const OslParameter* param = query.getparam(i);
        std::string propName = param->name.string();

        // Struct members are not supported
        if (propName.find('.') != std::string::npos) {
            continue;
        }

        // Extract metadata
        NdrTokenMap metadata = _getPropertyMetadata(param, discoveryResult);

        // Get type name, and determine the size of the array (if an array)
        TfToken typeName;
        size_t arraySize;
        std::tie(typeName, arraySize) = _getTypeName(param, metadata);

        _injectParserMetadata(metadata, typeName);

        // Non-standard properties in the metadata are considered hints
        NdrTokenMap hints;
        std::string  definitionName;
        for (auto metaIt = metadata.cbegin(); metaIt != metadata.cend(); ) {
            if (std::find(SdrPropertyMetadata->allTokens.begin(),
                          SdrPropertyMetadata->allTokens.end(),
                          metaIt->first) != SdrPropertyMetadata->allTokens.end()){
                metaIt++;
                continue;
            }

            if (metaIt->first == _tokens->sdrDefinitionName){
                definitionName = metaIt->second;
                metaIt = metadata.erase(metaIt);
                continue;
            }
            
            // The metadata sometimes incorrectly specifies array size; this
            // value is not respected
            if (metaIt->first == _tokens->arraySize) {
                TF_DEBUG(NDR_PARSING).Msg(
                    "Ignoring bad 'arraySize' attribute on property [%s] "
                    "on OSL shader [%s]",
                    propName.c_str(), discoveryResult.name.c_str());
                metaIt = metadata.erase(metaIt);
                continue;
            }

            hints.insert(*metaIt++);
        }

        // If we found 'definitionName' metadata, we actually need to 
        // change the name of the property to match, using the OSL
        // parameter name as the ImplementationName
        if (!definitionName.empty()){
            metadata[SdrPropertyMetadata->ImplementationName] = TfToken(propName);
            propName = definitionName;
        } else if (!fallbackPrefix.empty()){
            metadata[SdrPropertyMetadata->ImplementationName] = TfToken(propName);
            propName = TfToken(SdfPath::JoinIdentifier(fallbackPrefix, propName));
        }

        // Extract options
        NdrOptionVec options;
        if (metadata.count(SdrPropertyMetadata->Options)) {
            options = OptionVecVal(metadata.at(SdrPropertyMetadata->Options));
        }

        properties.emplace_back(
            SdrShaderPropertyUniquePtr(
                new SdrShaderProperty(
                    TfToken(propName),
                    typeName,
                    _getDefaultValue(
                        *param,
                        typeName,
                        arraySize,
                        metadata),
                    param->isoutput,
                    arraySize,
                    metadata,
                    hints,
                    options
                )
            )
        );
    }

    return properties;
}

NdrTokenMap
SdrOslParserPlugin::_getPropertyMetadata(const OslParameter* param,
    const NdrNodeDiscoveryResult& discoveryResult) const
{
    NdrTokenMap metadata;

    for (const OslParameter& metaParam : param->metadata) {
        TfToken entryName = TfToken(metaParam.name.string());

        // Vstruct metadata needs to be specially parsed; otherwise, just stuff
        // the value into the map
        if (entryName == _tokens->vstructMember) {
            std::string vstruct = _getParamAsString(metaParam);

            if (!vstruct.empty()) {
                // A dot splits struct from member name
                size_t dotPos = vstruct.find('.');

                if (dotPos != std::string::npos) {
                    metadata[SdrPropertyMetadata->VstructMemberOf] =
                        vstruct.substr(0, dotPos);

                    metadata[SdrPropertyMetadata->VstructMemberName] =
                        vstruct.substr(dotPos + 1);
                } else {
                TF_DEBUG(NDR_PARSING).Msg(
                    "Bad virtual structure member in %s.%s:%s",
                    discoveryResult.name.c_str(), param->name.string().c_str(),
                    vstruct.c_str());
                }
            }
        } else if (entryName == _tokens->pageStr) {
            // Replace OslPageDelimiter with SdrShaderProperty's Page Delimiter
            metadata[entryName] = TfStringReplace(
                    _getParamAsString(metaParam),
                    _tokens->oslPageDelimiter, 
                    SdrPropertyTokens->PageDelimiter.GetString());
        } else {
            metadata[entryName] = _getParamAsString(metaParam);
        }
    }

    return metadata;
}

void
SdrOslParserPlugin::_injectParserMetadata(NdrTokenMap& metadata,
                                          const TfToken& typeName) const
{
    if (typeName == SdrPropertyTypes->String) {
        if (IsPropertyAnAssetIdentifier(metadata)) {
            metadata[SdrPropertyMetadata->IsAssetIdentifier] = "";
        }
    }
}

NdrTokenMap
SdrOslParserPlugin::_getNodeMetadata(
    const OSL::OSLQuery &query,
    const NdrTokenMap &baseMetadata) const
{
    NdrTokenMap nodeMetadata = baseMetadata;

    // Convert the OSL metadata to a dict. Each entry in the metadata is stored
    // as an OslParameter.
    for (const OslParameter& metaParam : query.metadata()) {
        TfToken entryName = TfToken(metaParam.name.string());

        // Check for node metadata with the usdSchemaDef_ prefix and store the
        // metadata with the prefix removed.
        // XXX: Need to confirm if _getParamAsString handle vector values, 
        // when we have a use case for OSL shaders providing such metadata 
        // (for example, usdSchemaDef's apiSchemaAutoApplyTo)
        if (strncmp(_tokens->usdSchemaDefPrefix.GetText(), entryName.GetText(), 
            _tokens->usdSchemaDefPrefix.size()) == 0)
        {
            const std::string entrySubStr = (entryName.GetString()).substr(
                (_tokens->usdSchemaDefPrefix).size());
            nodeMetadata[TfToken(entrySubStr)] = _getParamAsString(metaParam); 
        }
        else if (strncmp(_tokens->sdrGlobalConfigPrefix.GetText(), 
            entryName.GetText(), _tokens->sdrGlobalConfigPrefix.size()) == 0)
        {
            const std::string entrySubStr = (entryName.GetString()).substr(
                (_tokens->sdrGlobalConfigPrefix).size());
            nodeMetadata[TfToken(entrySubStr)] = _getParamAsString(metaParam); 
        }
        else
        {
            nodeMetadata[entryName] = _getParamAsString(metaParam);
        }
    }

    return nodeMetadata;
}

std::string
SdrOslParserPlugin::_getParamAsString(const OslParameter& param) const
{
    if (param.sdefault.size() == 1) {
        return param.sdefault[0].string();
    } else if (param.idefault.size() == 1) {
        return std::to_string(param.idefault[0]);
    } else if (param.fdefault.size() == 1) {
        return std::to_string(param.fdefault[0]);
    }

    return std::string();
}

std::tuple<TfToken, size_t>
SdrOslParserPlugin::_getTypeName(
    const OslParameter* param,
    const NdrTokenMap& metadata) const
{
    // Exit early if this param is known to be a struct
    if (param->isstruct) {
        return std::make_tuple(SdrPropertyTypes->Struct, /* array size = */ 0);
    }

    // Exit early if the param's metadata indicates the param is a terminal type
    if (IsPropertyATerminal(metadata)) {
        return std::make_tuple(
            SdrPropertyTypes->Terminal, /* array size = */ 0);
    }

    // Otherwise, continue on to determine the type (and possibly array size)
    std::string typeName = std::string(param->type.c_str());
    size_t arraySize = 0;
    size_t openingBracket = typeName.find('[');

    if (openingBracket != std::string::npos) {
        try {
            // stoi will stop at the first non-number char, usually ']'
            arraySize = std::stoi(typeName.substr(openingBracket + 1));
        } catch (...) {
            // It is possible we try to parse a type like `color[]`, which
            // usually indicates a dynamic array type. This attribute NEEDS to
            // have the `isDynamicArray` metadatum set to 1, otherwise the Sdr
            // will not recognize it as an actual array type
        }

        // grab the part before the first bracket and turn it into the typeName
        typeName = typeName.substr(0, openingBracket);
    }

    return std::make_tuple(TfToken(typeName), arraySize);
}

VtValue
SdrOslParserPlugin::_getDefaultValue(
    const SdrOslParserPlugin::OslParameter& param,
    const std::string& oslType,
    size_t arraySize,
    const NdrTokenMap& metadata) const
{
    // Determine array-ness
    bool isDynamicArray =
        IsTruthy(SdrPropertyMetadata->IsDynamicArray, metadata);
    bool isArray = (arraySize > 0) || isDynamicArray;

    // INT and INT ARRAY
    // -------------------------------------------------------------------------
    if (oslType == SdrPropertyTypes->Int) {
        if (!isArray && param.idefault.size() == 1) {
            return VtValue(param.idefault[0]);
        }

        VtIntArray array;
        array.assign(param.idefault.begin(), param.idefault.end());

        return VtValue::Take(array);
    }

    // STRING and STRING ARRAY
    // -------------------------------------------------------------------------
    else if (oslType == SdrPropertyTypes->String) {

        // Handle non-array
        if (!isArray && param.sdefault.size() == 1) {
            return VtValue(param.sdefault[0].string());
        }

        // Handle array
        VtStringArray array;
        array.reserve(param.sdefault.size());

        // Strings are stored as `ustring`s from OIIO; these need to be
        // converted explicitly into `std::string`s (otherwise the VtStringArray
        // will contain garbage).
        for (const OIIO::ustring& ustr : param.sdefault) {
            array.push_back(ustr.string());
        }
        return VtValue::Take(array);
    }

    // FLOAT and FLOAT ARRAY
    // -------------------------------------------------------------------------
    else if (oslType == SdrPropertyTypes->Float) {
        if (!isArray && param.fdefault.size() == 1) {
            return VtValue(param.fdefault[0]);
        }

        VtFloatArray array;
        array.assign(param.fdefault.begin(), param.fdefault.end());

        return VtValue::Take(array);
    }

    // VECTOR TYPES and VECTOR TYPE ARRAYS
    // -------------------------------------------------------------------------
    else if (oslType == SdrPropertyTypes->Color  ||
             oslType == SdrPropertyTypes->Point  ||
             oslType == SdrPropertyTypes->Normal ||
             oslType == SdrPropertyTypes->Vector) {
        if (!isArray && param.fdefault.size() == 3) {
            return VtValue(
                GfVec3f(param.fdefault[0],
                        param.fdefault[1],
                        param.fdefault[2])
            );
        } else if (isArray && param.fdefault.size() % 3 == 0) {
            int numElements = param.fdefault.size() / 3;
            VtVec3fArray array(numElements);

            for (int i = 0; i < numElements; ++i) {
                array[i] = GfVec3f(param.fdefault[3*i + 0],
                                   param.fdefault[3*i + 1],
                                   param.fdefault[3*i + 2]);
            }

            return VtValue::Take(array);
        }
    }

    // MATRIX
    // -------------------------------------------------------------------------
    else if (oslType == SdrPropertyTypes->Matrix) {
        // XXX: No matrix array support
        if (!isArray && param.fdefault.size() == 16) {
            GfMatrix4d mat;
            double* values = mat.GetArray();

            for (int i = 0; i < 16; ++i) {
                values[i] = static_cast<double>(param.fdefault[i]);
            }

            return VtValue::Take(mat);
        }
    }

    // STRUCT, TERMINAL, VSTRUCT
    // -------------------------------------------------------------------------
    else if (oslType == SdrPropertyTypes->Struct ||
             oslType == SdrPropertyTypes->Terminal ||
             oslType == SdrPropertyTypes->Vstruct) {
        // We return an empty VtValue for Struct, Terminal, and Vstruct
        // properties because their value may rely on being computed within the
        // renderer, or we might not have a reasonable way to represent their
        // value within Sdr
        return VtValue();
    }

    // Didn't find a supported type
    return VtValue();
}

PXR_NAMESPACE_CLOSE_SCOPE
