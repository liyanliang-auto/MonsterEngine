import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

// Host executables are supplied with -P<name>=... or the same environment variable.
// No workstation-specific SDK/tool paths belong in this project.
val hostToolNames = listOf("SPLAT_GLSLC", "SPLAT_SPIRV_VAL", "Python3_EXECUTABLE")
val hostTools = hostToolNames.mapNotNull { name ->
    providers.gradleProperty(name).orElse(providers.environmentVariable(name)).orNull
        ?.takeIf { it.isNotBlank() }?.let { name to it }
}.toMap()
val fpsOverlayEnabled = providers.gradleProperty("MONSTER_ANDROID_FPS_OVERLAY")
    .map { it.equals("ON", ignoreCase = true) || it.equals("true", ignoreCase = true) }
    .orElse(true).get()
val signingNames = listOf("MONSTER_KEYSTORE", "MONSTER_KEYSTORE_PASSWORD", "MONSTER_KEY_ALIAS", "MONSTER_KEY_PASSWORD")
val signingValues = signingNames.associateWith { providers.environmentVariable(it).orNull }
val signingAvailable = signingValues.values.all { !it.isNullOrBlank() }
check(signingAvailable || signingValues.values.all { it.isNullOrBlank() }) {
    "Release signing requires all four MONSTER_KEYSTORE / MONSTER_KEYSTORE_PASSWORD / MONSTER_KEY_ALIAS / MONSTER_KEY_PASSWORD environment variables."
}

android {
    namespace = "com.monsterengine.viewer"
    compileSdk = 36
    buildToolsVersion = "36.1.0"
    ndkVersion = "30.0.16248370"
    defaultConfig {
        applicationId = "com.monsterengine.viewer"
        minSdk = 26
        targetSdk = 36
        versionCode = providers.gradleProperty("monsterVersionCode").orElse("1").get().toInt()
        versionName = providers.gradleProperty("monsterVersionName").orElse("0.1.0").get()
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake {
                targets += "monster_android"
                arguments += listOf("-DANDROID_STL=c++_static", "-DMONSTER_BUILD_ENGINE=ON",
                    "-DMONSTER_ANDROID_FPS_OVERLAY=${if (fpsOverlayEnabled) "ON" else "OFF"}")
                arguments += hostTools.map { (name, path) -> "-D$name=$path" }
            }
        }
        manifestPlaceholders["monsterProfileable"] = "false"
    }
    signingConfigs {
        if (signingAvailable) {
            create("externalRelease") {
                storeFile = file(signingValues.getValue("MONSTER_KEYSTORE")!!)
                storePassword = signingValues.getValue("MONSTER_KEYSTORE_PASSWORD")
                keyAlias = signingValues.getValue("MONSTER_KEY_ALIAS")
                keyPassword = signingValues.getValue("MONSTER_KEY_PASSWORD")
            }
        }
    }
    buildTypes {
        getByName("debug") {
            applicationIdSuffix = ".debug"
            isDebuggable = true
            isJniDebuggable = true
        }
        getByName("release") {
            isDebuggable = false
            isJniDebuggable = false
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (signingAvailable) signingConfig = signingConfigs.getByName("externalRelease")
        }
        create("profile") {
            initWith(getByName("release"))
            applicationIdSuffix = ".profile"
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
            manifestPlaceholders["monsterProfileable"] = "true"
            matchingFallbacks += "release"
        }
        all {
            val assetRoot = layout.buildDirectory.dir("generated/monsterAssets/$name").get().asFile
            externalNativeBuild.cmake.arguments += "-DMONSTER_ANDROID_ASSET_DIR=${assetRoot.invariantSeparatorsPath}"
            // Assets are ABI-independent; each native build writes a separate tree.
            sourceSets.getByName(name).assets.srcDir(assetRoot.resolve("arm64-v8a"))
        }
    }
    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }
    packaging {
        jniLibs {
            // AGP >=8.5.1 aligns uncompressed shared libraries to 16 KiB.
            useLegacyPackaging = false
        }
    }
    androidResources { noCompress += listOf("spv", "ply", "mgs", "mgp") }
    buildFeatures { buildConfig = true }
    buildTypes.all { buildConfigField("boolean", "FPS_OVERLAY_ENABLED", fpsOverlayEnabled.toString()) }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
kotlin {
    jvmToolchain(21)
    compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
}

// Native CMake dependencies produce validated shaders and the externally derived built-in scene.
// Asset merging must never silently package an empty or stale generated directory.
tasks.configureEach {
    if (name in setOf("mergeDebugAssets", "mergeProfileAssets", "mergeReleaseAssets")) {
        val variantName = name.removePrefix("merge").removeSuffix("Assets")
        dependsOn("externalNativeBuild$variantName")
    }
}
dependencies { implementation(libs.androidx.core) }
