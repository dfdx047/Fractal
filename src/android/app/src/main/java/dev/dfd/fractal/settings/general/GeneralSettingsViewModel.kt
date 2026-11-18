package dev.dfd.fractal.settings.general

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dev.dfd.fractal.common.settings.AppSettings
import dev.dfd.fractal.common.settings.AppSettingsStore
import dev.dfd.fractal.common.settings.EmulationSettings
import dev.dfd.fractal.common.settings.GamePadPosition
import dev.dfd.fractal.common.ui.localization.getAvailableLanguages
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

class GeneralSettingsViewModel(
    private val dataStore: DataStore<AppSettings> = AppSettingsStore.dataStore,
) : ViewModel() {
    val languages: List<String>
    val languageToDisplayNameMap: Map<String, String>
    val emulationSettings = dataStore.data.map { it.emulationSettings }
        .stateIn(
            viewModelScope,
            SharingStarted.WhileSubscribed(5000),
            EmulationSettings(),
        )

    init {
        val availableLanguages = getAvailableLanguages()
        languages = availableLanguages.map { it.code }
        languageToDisplayNameMap = availableLanguages.associateBy({ it.code }, { it.displayName })
    }

    fun setLanguage(language: String, context: Context) {
        dev.dfd.fractal.common.ui.localization.setLanguage(language, context)

        viewModelScope.launch {
            dataStore.updateData { it.copy(guiSettings = it.guiSettings.copy(language = language)) }
        }
    }

    fun setGamePadPosition(gamePadPosition: GamePadPosition) {
        viewModelScope.launch {
            dataStore.updateData {
                it.copy(
                    emulationSettings = it.emulationSettings.copy(
                        gamePadPosition = gamePadPosition
                    )
                )
            }
        }
    }
}