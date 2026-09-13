plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.ksp)
}

android {
    namespace = "com.handoff.band"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.handoff.band"

        // CompanionDeviceManager arrived in 26 and the whole pairing story
        // depends on it, so there is no point supporting anything older.
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = "0.2"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"),
                          "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.service)
    implementation(libs.androidx.activity.compose)

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.navigation.compose)

    // The QR label on the band (design decisions 2a). ZXing runs offline and
    // adds nothing but a camera activity; ML Kit would pull in Play services.
    implementation(libs.zxing.android.embedded)

    implementation(libs.androidx.room.runtime)
    implementation(libs.androidx.room.ktx)
    ksp(libs.androidx.room.compiler)

    // Chunk.kt and VCard.kt are plain Kotlin with no Android dependency, for
    // exactly the reason lib/link/chunk.c is plain C: the framing has to be
    // testable without the thing it frames.
    testImplementation(libs.junit)
}
