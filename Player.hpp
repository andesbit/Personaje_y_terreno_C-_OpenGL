 
/*
    Copyright 2025 Adolfo Cárdenas P.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp> // For glm::quat and glm::slerp
#include <glm/gtc/constants.hpp> // For glm::pi
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm> // For std::min/max
#include <limits>    // For std::numeric_limits
#include <memory>    // For std::unique_ptr

// Necessary for stb_image.h
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // Make sure this file is in your project

// Bone structure
struct Bone {
    std::string name;
    int id; // Unique ID for the bone, used as an index in boneTransforms
    glm::mat4 offsetMatrix;
    glm::mat4 finalTransformation;
};

// Animation information structure
struct Animation {
    std::string name;
    double duration;
    double ticksPerSecond;
    std::map<std::string, std::vector<aiVectorKey>> positionKeyframes;
    std::map<std::string, std::vector<aiQuatKey>> rotationKeyframes;
    std::map<std::string, std::vector<aiVectorKey>> scalingKeyframes;
};

// Mesh data structure
struct Mesh {
    GLuint VAO, VBO, EBO;
    GLuint boneIDVBO;    // VBO for bone IDs
    GLuint boneWeightVBO; // VBO for bone weights
    unsigned int indexCount;
    GLuint textureID; // Texture ID for this mesh
};

// Animated model class
class AnimatedModel {
private:
    Assimp::Importer importer; 

    std::map<std::string, Bone> bones;
    std::vector<glm::mat4> boneTransforms;
    std::vector<Animation> animations;
    const aiScene* scene;
    std::vector<Mesh> meshes;
    float animationTime = 0.0f;
    glm::mat4 globalInverseTransform;

    int boneCounter = 0; // Counter to assign unique bone IDs
    std::string directory; // Base directory of the model for loading textures.
    std::vector<GLuint> textures_loaded; // To avoid loading the same texture multiple times.

    // Vertex Shader (for Animated Model - UNCHANGED)
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in ivec4 boneIDs;
        layout (location = 3) in vec4 boneWeights;
        layout (location = 4) in vec2 aTexCoords; // Texture coordinates

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        uniform mat4 bones[100]; // Max 100 bones

        out vec3 Normal;
        out vec3 FragPos;
        out vec2 TexCoords; // Pass to Fragment Shader

        void main() {
            mat4 boneTransform = mat4(1.0); // Default to identity matrix (no bone influence)
            
            // Only apply bone transformation if there are significant bone weights
            if (dot(boneWeights, boneWeights) > 0.0001) { 
                boneTransform = bones[boneIDs[0]] * boneWeights[0];
                boneTransform += bones[boneIDs[1]] * boneWeights[1];
                boneTransform += bones[boneIDs[2]] * boneWeights[2];
                boneTransform += bones[boneIDs[3]] * boneWeights[3];
            }

            vec4 pos = boneTransform * vec4(aPos, 1.0);
            gl_Position = projection * view * model * pos;
            FragPos = vec3(model * pos);
            Normal = mat3(transpose(inverse(model))) * (boneTransform * vec4(aNormal, 0.0)).xyz;
            TexCoords = aTexCoords; // Assign texture coordinates
        }
    )";

    // Fragment Shader (for Animated Model - REVERTED TO TEXTURE SAMPLING AND LIGHTING)
    const char* fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        in vec3 Normal;
        in vec3 FragPos;
        in vec2 TexCoords;

        uniform vec3 lightPos;
        uniform vec3 viewPos;
        uniform sampler2D ourTexture; 
        uniform vec3 lightColor;
        uniform float ambientStrength;
        uniform float diffuseStrength;

        void main() {
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);

            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor * diffuseStrength; 

            vec3 ambient = ambientStrength * lightColor; 

            vec3 texColor = texture(ourTexture, TexCoords).rgb; // Sample texture
            
            FragColor = vec4(texColor * (ambient + diffuse), 1.0);
        }
    )";

    glm::mat4 convertMatrix(const aiMatrix4x4& from) {
        glm::mat4 to;
        to[0][0] = from.a1; to[0][1] = from.b1; to[0][2] = from.c1; to[0][3] = from.d1;
        to[1][0] = from.a2; to[1][1] = from.b2; to[1][2] = from.c2; to[1][3] = from.d2;
        to[2][0] = from.a3; to[2][1] = from.b3; to[2][2] = from.c3; to[2][3] = from.d3;
        to[3][0] = from.a4; to[3][1] = from.b4; to[3][2] = from.c4; to[3][3] = from.d4;
        return to;
    }

    void processNode(aiNode* node, const aiScene* scene) {
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            processMesh(mesh);
        }
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    void processMesh(aiMesh* mesh) {
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        std::vector<float> boneWeightsData;
        std::vector<int> boneIDsData;

        boneWeightsData.resize(mesh->mNumVertices * 4, 0.0f);
        boneIDsData.resize(mesh->mNumVertices * 4, -1);

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            vertices.push_back(mesh->mVertices[i].x);
            vertices.push_back(mesh->mVertices[i].y);
            vertices.push_back(mesh->mVertices[i].z);
            vertices.push_back(mesh->mNormals[i].x);
            vertices.push_back(mesh->mNormals[i].y);
            vertices.push_back(mesh->mNormals[i].z);
            if (mesh->mTextureCoords[0]) {
                vertices.push_back(mesh->mTextureCoords[0][i].x);
                vertices.push_back(mesh->mTextureCoords[0][i].y);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(0.0f);
            }
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }

        for (unsigned int i = 0; i < mesh->mNumBones; i++) {
            aiBone* bone = mesh->mBones[i];
            std::string boneName = bone->mName.C_Str();

            int boneIndex = -1;
            auto it = bones.find(boneName);
            if (it != bones.end()) {
                boneIndex = it->second.id;
            } else {
                std::cerr << "Warning: Bone '" << boneName << "' not found in bone map during mesh processing. This bone will not influence vertices." << std::endl;
                continue;
            }

            // CORRECTED LINE: Loop through bone->mNumWeights, not mWeights[j].mNumWeights
            for (unsigned int j = 0; j < bone->mNumWeights; j++) {
                aiVertexWeight weight = bone->mWeights[j];
                unsigned int vertexID = weight.mVertexId;

                for (int k = 0; k < 4; k++) {
                    if (boneWeightsData[vertexID * 4 + k] == 0.0f) {
                        boneWeightsData[vertexID * 4 + k] = weight.mWeight;
                        boneIDsData[vertexID * 4 + k] = boneIndex;
                        break;
                    }
                }
            }
        }

        Mesh m;
        glGenVertexArrays(1, &m.VAO);
        glGenBuffers(1, &m.VBO);
        glGenBuffers(1, &m.EBO);

        glBindVertexArray(m.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(4);

        glGenBuffers(1, &m.boneIDVBO);
        glBindBuffer(GL_ARRAY_BUFFER, m.boneIDVBO);
        glBufferData(GL_ARRAY_BUFFER, boneIDsData.size() * sizeof(int), boneIDsData.data(), GL_STATIC_DRAW);
        glVertexAttribIPointer(2, 4, GL_INT, 4 * sizeof(int), (void*)0);
        glEnableVertexAttribArray(2);

        glGenBuffers(1, &m.boneWeightVBO);
        glBindBuffer(GL_ARRAY_BUFFER, m.boneWeightVBO);
        glBufferData(GL_ARRAY_BUFFER, boneWeightsData.size() * sizeof(float), boneWeightsData.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(3);

        glBindVertexArray(0);

        m.indexCount = indices.size();
        
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            m.textureID = loadMaterialTextures(material, aiTextureType_DIFFUSE);
        } else {
            m.textureID = 0;
        }

        meshes.push_back(m);
    }

    GLuint loadMaterialTextures(aiMaterial *mat, aiTextureType type) {
        aiString str;
        mat->GetTexture(type, 0, &str);
        std::string filename = std::string(str.C_Str());
        std::string fullPath;

        // Check if the filename from Assimp is already an absolute path
        // (e.g., starts with C:/ or /home/)
        if (filename.length() > 2 && filename[1] == ':' && (filename[0] >= 'A' && filename[0] <= 'Z')) { // Windows absolute path
            fullPath = filename;
        } else if (filename.length() > 0 && (filename[0] == '/' || filename.find(":/") != std::string::npos)) { // Linux/macOS absolute path or general absolute path
            fullPath = filename;
        } else {
            // Otherwise, assume it's relative to the model's directory
            fullPath = directory + "/" + filename;
        }

        GLuint textureID;
        glGenTextures(1, &textureID);

        int width, height, nrComponents;
        unsigned char *data = stbi_load(fullPath.c_str(), &width, &height, &nrComponents, 0);
        if (data) {
            GLenum format;
            if (nrComponents == 1)
                format = GL_RED;
            else if (nrComponents == 3)
                format = GL_RGB;
            else if (nrComponents == 4)
                format = GL_RGBA;
            else {
                std::cerr << "Unsupported texture format for: " << fullPath << std::endl;
                stbi_image_free(data);
                glDeleteTextures(1, &textureID);
                return 0;
            }

            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            stbi_image_free(data);
            std::cout << "Texture loaded: " << fullPath << std::endl;
            textures_loaded.push_back(textureID);
            return textureID;
        } else {
            std::cerr << "Texture failed to load at path: " << fullPath << std::endl;
            // Optionally try to load a default texture if a specific one fails
            // Or ensure a valid 0 is returned to indicate no texture.
            glDeleteTextures(1, &textureID); // Clean up the texture ID
            return 0; // Return 0 to indicate no texture loaded
        }
    }


    void loadBones(aiMesh* mesh) {
        for (unsigned int i = 0; i < mesh->mNumBones; i++) {
            aiBone* bone = mesh->mBones[i];
            std::string boneName = bone->mName.C_Str();

            if (bones.find(boneName) == bones.end()) {
                Bone b;
                b.name = boneName;
                b.offsetMatrix = convertMatrix(bone->mOffsetMatrix);
                b.id = boneCounter++;
                bones[boneName] = b;
                // std::cout << "Loaded bone: " << b.name << " with ID: " << b.id << std::endl; // Too verbose
            }
        }
    }

    void loadAnimations() {
        for (unsigned int i = 0; i < scene->mNumAnimations; i++) {
            aiAnimation* anim = scene->mAnimations[i];
            Animation animation;
            animation.name = anim->mName.C_Str();
            animation.duration = anim->mDuration;
            animation.ticksPerSecond = anim->mTicksPerSecond ? anim->mTicksPerSecond : 25.0f;

            for (unsigned int j = 0; j < anim->mNumChannels; j++) { 
                aiNodeAnim* channel = anim->mChannels[j];
                std::string boneName = channel->mNodeName.C_Str();

                std::vector<aiVectorKey> posKeys;
                for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                    posKeys.push_back(channel->mPositionKeys[k]);
                }
                animation.positionKeyframes[boneName] = posKeys;

                std::vector<aiQuatKey> rotKeys;
                for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                    rotKeys.push_back(channel->mRotationKeys[k]);
                }
                animation.rotationKeyframes[boneName] = rotKeys;

                std::vector<aiVectorKey> scaleKeys;
                for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                    scaleKeys.push_back(channel->mScalingKeys[k]);
                }
                animation.scalingKeyframes[boneName] = scaleKeys;
            }
            animations.push_back(animation);
        }
    }

    glm::mat4 getInterpolatedBoneTransform(const Animation& anim, const std::string& boneName, float animTime) {
        glm::mat4 translationMatrix = glm::mat4(1.0f);
        glm::mat4 rotationMatrix = glm::mat4(1.0f);
        glm::mat4 scaleMatrix = glm::mat4(1.0f);

        auto posItr = anim.positionKeyframes.find(boneName);
        if (posItr != anim.positionKeyframes.end() && !posItr->second.empty()) {
            const auto& positionKeys = posItr->second;
            if (positionKeys.size() == 1) {
                translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(positionKeys[0].mValue.x, positionKeys[0].mValue.y, positionKeys[0].mValue.z));
            } else {
                int frame = 0;
                for (int i = 0; i < positionKeys.size() - 1; ++i) {
                    if (animTime < positionKeys[i + 1].mTime) {
                        frame = i;
                        break;
                    }
                }
                int nextFrame = (frame + 1) % positionKeys.size();
                // Ensure nextFrame is not out of bounds or same as current frame
                if (positionKeys[nextFrame].mTime == positionKeys[frame].mTime || positionKeys.size() < 2) {
                    translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(positionKeys[frame].mValue.x, positionKeys[frame].mValue.y, positionKeys[frame].mValue.z));
                } else {
                    float t = (animTime - positionKeys[frame].mTime) / (positionKeys[nextFrame].mTime - positionKeys[frame].mTime);
                    glm::vec3 startPos(positionKeys[frame].mValue.x, positionKeys[frame].mValue.y, positionKeys[frame].mValue.z);
                    glm::vec3 endPos(positionKeys[nextFrame].mValue.x, positionKeys[nextFrame].mValue.y, positionKeys[nextFrame].mValue.z);
                    translationMatrix = glm::translate(glm::mat4(1.0f), glm::mix(startPos, endPos, t));
                }
            }
        }

        auto rotItr = anim.rotationKeyframes.find(boneName);
        if (rotItr != anim.rotationKeyframes.end() && !rotItr->second.empty()) {
            const auto& rotationKeys = rotItr->second;
            if (rotationKeys.size() == 1) {
                rotationMatrix = glm::mat4_cast(glm::quat(rotationKeys[0].mValue.w, rotationKeys[0].mValue.x, rotationKeys[0].mValue.y, rotationKeys[0].mValue.z));
            } else {
                int frame = 0;
                for (int i = 0; i < rotationKeys.size() - 1; ++i) {
                    if (animTime < rotationKeys[i + 1].mTime) {
                        frame = i;
                        break;
                    }
                }
                int nextFrame = (frame + 1) % rotationKeys.size();
                 if (rotationKeys[nextFrame].mTime == rotationKeys[frame].mTime || rotationKeys.size() < 2) {
                    rotationMatrix = glm::mat4_cast(glm::quat(rotationKeys[frame].mValue.w, rotationKeys[frame].mValue.x, rotationKeys[frame].mValue.y, rotationKeys[frame].mValue.z));
                } else {
                    float t = (animTime - rotationKeys[frame].mTime) / (rotationKeys[nextFrame].mTime - rotationKeys[frame].mTime);
                    glm::quat startRot(rotationKeys[frame].mValue.w, rotationKeys[frame].mValue.x, rotationKeys[frame].mValue.y, rotationKeys[frame].mValue.z);
                    glm::quat endRot(rotationKeys[nextFrame].mValue.w, rotationKeys[nextFrame].mValue.x, rotationKeys[nextFrame].mValue.y, rotationKeys[nextFrame].mValue.z);
                    rotationMatrix = glm::mat4_cast(glm::slerp(startRot, endRot, t));
                }
            }
        }

        auto scaleItr = anim.scalingKeyframes.find(boneName);
        if (scaleItr != anim.scalingKeyframes.end() && !scaleItr->second.empty()) {
            const auto& scaleKeys = scaleItr->second;
            if (scaleKeys.size() == 1) {
                scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(scaleKeys[0].mValue.x, scaleKeys[0].mValue.y, scaleKeys[0].mValue.z));
            } else {
                int frame = 0;
                for (int i = 0; i < scaleKeys.size() - 1; ++i) {
                    if (animTime < scaleKeys[i + 1].mTime) {
                        frame = i;
                        break;
                    }
                }
                int nextFrame = (frame + 1) % scaleKeys.size();
                if (scaleKeys[nextFrame].mTime == scaleKeys[frame].mTime || scaleKeys.size() < 2) {
                    scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(scaleKeys[frame].mValue.x, scaleKeys[frame].mValue.y, scaleKeys[frame].mValue.z));
                } else {
                    float t = (animTime - scaleKeys[frame].mTime) / (scaleKeys[nextFrame].mTime - scaleKeys[frame].mTime);
                    glm::vec3 startScale(scaleKeys[frame].mValue.x, scaleKeys[frame].mValue.y, scaleKeys[frame].mValue.z);
                    glm::vec3 endScale(scaleKeys[nextFrame].mValue.x, scaleKeys[nextFrame].mValue.y, scaleKeys[nextFrame].mValue.z);
                    scaleMatrix = glm::scale(glm::mat4(1.0f), glm::mix(startScale, endScale, t));
                }
            }
        }

        return translationMatrix * rotationMatrix * scaleMatrix;
    }

    void calculateBoneTransformations(aiNode* node, glm::mat4 parentTransform) {
        std::string nodeName = node->mName.C_Str();
        glm::mat4 nodeTransformation = convertMatrix(node->mTransformation);

        if (bones.count(nodeName)) {
            if (!animations.empty()) {
                Animation& currentAnimation = animations[0]; // Assuming only one animation per model
                glm::mat4 interpolatedLocalTransform = getInterpolatedBoneTransform(currentAnimation, nodeName, animationTime);
                nodeTransformation = interpolatedLocalTransform;
            }
        }

        glm::mat4 globalTransformation = parentTransform * nodeTransformation;

        auto boneIt = bones.find(nodeName);
        if (boneIt != bones.end()) {
            Bone& bone = boneIt->second;
            bone.finalTransformation = globalTransformation * bone.offsetMatrix;
            
            if (bone.id < boneTransforms.size()) {
                boneTransforms[bone.id] = bone.finalTransformation;
            } else {
                std::cerr << "Error: bone.id " << bone.id << " out of bounds for boneTransforms (size " << boneTransforms.size() << ")" << std::endl;
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            calculateBoneTransformations(node->mChildren[i], globalTransformation);
        }
    }

public:
    GLuint shaderProgram;
    glm::vec3 modelCenter;

    // Constructor
    AnimatedModel(const std::string& path) : boneCounter(0) {
        // Shader setup
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        GLint success;
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            GLchar infoLog[512];
            glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
            std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
        }
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            GLchar infoLog[512];
            glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
            std::cerr << "Shader program linking failed: " << infoLog << std::endl;
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Get the directory of the model file
        size_t lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            directory = path.substr(0, lastSlash);
        } else {
            directory = "."; // If no path, assume current directory
        }
        
        scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs |
                                aiProcess_CalcTangentSpace | aiProcess_ValidateDataStructure);
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cerr << "Error loading model: " << importer.GetErrorString() << std::endl;
            return; 
        }

        std::cout << "Model loaded successfully. Meshes: " << scene->mNumMeshes
                    << ", Animations: " << scene->mNumAnimations << std::endl;

        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            loadBones(scene->mMeshes[i]);
        }
        
        boneTransforms.resize(bones.size(), glm::mat4(1.0f));
        std::cout << "Resized boneTransforms to " << boneTransforms.size() << " elements." << std::endl;

        processNode(scene->mRootNode, scene);
        loadAnimations();

        globalInverseTransform = glm::inverse(convertMatrix(scene->mRootNode->mTransformation));

        modelCenter = calculateModelCenter(scene);
        std::cout << "Model center: (" << modelCenter.x << ", " << modelCenter.y << ", " << modelCenter.z << ")" << std::endl;
    }

    // Destructor to free OpenGL resources
    ~AnimatedModel() {
        glDeleteProgram(shaderProgram);
        for (const auto& mesh : meshes) {
            glDeleteVertexArrays(1, &mesh.VAO);
            glDeleteBuffers(1, &mesh.VBO);
            glDeleteBuffers(1, &mesh.EBO);
            glDeleteBuffers(1, &mesh.boneIDVBO);
            glDeleteBuffers(1, &mesh.boneWeightVBO);
            if (mesh.textureID != 0) {
                glDeleteTextures(1, &mesh.textureID);
            }
        }
    }

    void updateAnimation(float deltaTime) {
        if (animations.empty()) {
            for (auto const& [name, bone] : bones) {
                if (bone.id < boneTransforms.size()) {
                    boneTransforms[bone.id] = bone.offsetMatrix;
                }
            }
            return;
        }

        Animation& currentAnimation = animations[0];
        animationTime += deltaTime * currentAnimation.ticksPerSecond;
        animationTime = fmod(animationTime, (float)currentAnimation.duration);

        calculateBoneTransformations(scene->mRootNode, glm::mat4(1.0f));
    }

    void Draw() {
        // Uniforms for bones are set here, before drawing any mesh
        for (size_t i = 0; i < boneTransforms.size(); ++i) {
            std::string uniformName = "bones[" + std::to_string(i) + "]";
            GLint uniformLoc = glGetUniformLocation(shaderProgram, uniformName.c_str());
            // No need for a warning if uniformLoc is -1, as unused bones might be optimized out.
            if (uniformLoc != -1) {
                glUniformMatrix4fv(uniformLoc, 1, GL_FALSE, glm::value_ptr(boneTransforms[i]));
            }
        }

        for (const Mesh& mesh : meshes) {
            if (mesh.textureID != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mesh.textureID);
                glUniform1i(glGetUniformLocation(shaderProgram, "ourTexture"), 0); 
            } else {
                // If no texture, bind a default white texture or handle as needed
                // For now, if no texture, it might appear black or default
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0); // Unbind any texture
                // You might want to set a default color in the shader here if no texture is found
            }

            glBindVertexArray(mesh.VAO);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);

            if (mesh.textureID != 0) {
                glBindTexture(GL_TEXTURE_2D, 0); // Unbind texture after use
            }
        }
    }

    glm::vec3 calculateModelCenter(const aiScene* scene) {
        glm::vec3 minBounds(std::numeric_limits<float>::max()), maxBounds(std::numeric_limits<float>::lowest());
        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[i];
            for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
                aiVector3D pos = mesh->mVertices[j];
                minBounds.x = std::min(minBounds.x, pos.x);
                minBounds.y = std::min(minBounds.y, pos.y);
                minBounds.z = std::min(minBounds.z, pos.z);
                maxBounds.x = std::max(maxBounds.x, pos.x);
                maxBounds.y = std::max(maxBounds.y, pos.y);
                maxBounds.z = std::max(maxBounds.z, pos.z);
            }
        }
        return (minBounds + maxBounds) * 0.5f;
    }
};
