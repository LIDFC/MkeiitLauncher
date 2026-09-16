// SPDX-License-Identifier: GPL-3.0-only
#include "OurServerTranslator.h"

#include <QHash>
#include <QStringList>

namespace {
struct TranslationEntry {
    const char* context;
    const char* source;
    QStringList forms;  // one form, or the three Russian plural forms
};

QString tableKey(const char* context, const char* source)
{
    return QString::fromUtf8(context) + QChar(4) + QString::fromUtf8(source);
}

const QHash<QString, QStringList>& translations()
{
    static const QHash<QString, QStringList> s_table = [] {
        const QList<TranslationEntry> entries = {
            // server pack manifest and Modrinth
            { "ServerPack", "The server pack manifest is not valid JSON.", { "Манифест серверной сборки не является корректным JSON." } },
            { "ServerPack",
              "The server pack manifest format version is not supported.",
              { "Версия формата манифеста серверной сборки не поддерживается." } },
            { "ServerPack",
              "The server pack manifest has an invalid pack version.",
              { "В манифесте серверной сборки указана некорректная версия сборки." } },
            { "ServerPack",
              "The server pack manifest has an invalid Minecraft version.",
              { "В манифесте серверной сборки указана некорректная версия Minecraft." } },
            { "ServerPack",
              "The server pack manifest has an unsupported or invalid loader.",
              { "В манифесте серверной сборки указан неподдерживаемый или некорректный загрузчик модов." } },
            { "ServerPack",
              "The server pack manifest has an invalid server address or port.",
              { "В манифесте серверной сборки указан некорректный адрес или порт сервера." } },
            { "ServerPack",
              "The server pack manifest has an invalid mod list.",
              { "В манифесте серверной сборки некорректный список модов." } },
            { "ServerPack",
              "The server pack manifest entry \"%1\" is invalid.",
              { "Запись «%1» в манифесте серверной сборки некорректна." } },
            { "ServerPack",
              "The server pack manifest entry \"%1\" uses an unsupported source.",
              { "Запись «%1» в манифесте серверной сборки использует неподдерживаемый источник." } },
            { "ServerPack",
              "The server pack manifest lists \"%1\" more than once.",
              { "Мод «%1» указан в манифесте серверной сборки несколько раз." } },
            { "ServerPack", "Modrinth returned an unexpected response.", { "Modrinth вернул неожиданный ответ." } },
            { "ServerPack", "The version of \"%1\" was not found on Modrinth.", { "Версия мода «%1» не найдена на Modrinth." } },
            { "ServerPack",
              "The Modrinth version of \"%1\" belongs to a different project.",
              { "Версия мода «%1» на Modrinth относится к другому проекту." } },
            { "ServerPack", "\"%1\" %2 does not support Minecraft %3.", { "«%1» %2 не поддерживает Minecraft %3." } },
            { "ServerPack", "\"%1\" %2 does not support the %3 loader.", { "«%1» %2 не поддерживает загрузчик %3." } },
            { "ServerPack",
              "The SHA-512 of \"%1\" does not match any file of its Modrinth version.",
              { "SHA-512 мода «%1» не совпадает ни с одним файлом его версии на Modrinth." } },
            { "ServerPack",
              "\"%1\" is not downloaded from a trusted Modrinth address.",
              { "Мод «%1» не скачивается с доверенного адреса Modrinth." } },
            { "ServerPack", "\"%1\" has an unsafe file name.", { "У мода «%1» небезопасное имя файла." } },
            { "ServerPack",
              "Several mods of the server pack use the file name \"%1\".",
              { "Несколько модов серверной сборки используют одно имя файла «%1»." } },

            // server pack task
            { "ServerPackTask", "The server pack instance is missing.", { "Экземпляр для серверной сборки не найден." } },
            { "ServerPackTask",
              "The server pack manifest URL must use HTTPS.",
              { "Адрес манифеста серверной сборки должен использовать HTTPS." } },
            { "ServerPackTask", "Checking the server pack...", { "Проверка серверной сборки..." } },
            { "ServerPackTask", "Could not download the server pack manifest: %1", { "Не удалось скачать манифест серверной сборки: %1" } },
            { "ServerPackTask", "Could not reach Modrinth: %1", { "Не удалось связаться с Modrinth: %1" } },
            { "ServerPackTask",
              "These files in the mods folder are not part of the server pack and block its installation: %1",
              { "Эти файлы в папке модов не относятся к серверной сборке и мешают её установке: %1" } },
            { "ServerPackTask", "Could not create the mods folder.", { "Не удалось создать папку модов." } },
            { "ServerPackTask", "Downloading server pack...", { "Скачивание серверной сборки..." } },
            { "ServerPackTask", "\"%1\" has an unsafe file name.", { "У мода «%1» небезопасное имя файла." } },
            { "ServerPackTask", "Downloading %1...", { "Скачивание %1..." } },
            { "ServerPackTask", "%1 of %n file(s)", { "%1 из %n файла", "%1 из %n файлов", "%1 из %n файлов" } },
            { "ServerPackTask", "Could not download the server pack: %1", { "Не удалось скачать серверную сборку: %1" } },
            { "ServerPackTask", "Could not save the server pack state: %1", { "Не удалось сохранить состояние серверной сборки: %1" } },

            // server status
            { "ServerStatus", "The server sent an invalid status response.", { "Сервер прислал некорректный ответ о своём состоянии." } },
            { "ServerStatus", "The server sent an invalid query response.", { "Сервер прислал некорректный ответ query." } },
            { "ServerStatusTask", "Checking the server status...", { "Проверка состояния сервера..." } },
            { "ServerStatusTask", "The server address could not be resolved.", { "Не удалось найти адрес сервера." } },
            { "ServerStatusTask", "Could not connect to the server: %1", { "Не удалось подключиться к серверу: %1" } },
            { "ServerStatusTask", "The server did not respond in time.", { "Сервер не ответил вовремя." } },
            { "ServerStatusTask", "The query port is not reachable: %1", { "Порт query недоступен: %1" } },
            { "ServerStatusTask", "The query port did not respond in time.", { "Порт query не ответил вовремя." } },

            // server instance
            { "OurServer::CreateInstanceTask", "Creating the server instance...", { "Создание экземпляра для сервера..." } },

            // "Our Server" category
            { "OurServerPage", "Our Server", { "Наш сервер" } },
            { "OurServerPage", "Server", { "Сервер" } },
            { "OurServerPage", "Address", { "Адрес" } },
            { "OurServerPage", "Loader", { "Загрузчик" } },
            { "OurServerPage", "Server pack", { "Серверная сборка" } },
            { "OurServerPage", "Mods of the server pack", { "Моды серверной сборки" } },
            { "OurServerPage", "Server pack %1", { "Серверная сборка %1" } },
            { "OurServerPage", "Mod", { "Мод" } },
            { "OurServerPage", "Status", { "Статус" } },
            { "OurServerPage", "Installed", { "Установлено" } },
            { "OurServerPage", "Required", { "Требуется" } },
            { "OurServerPage", "▶ Play on server", { "▶ Играть на сервере" } },
            { "OurServerPage", "Check again", { "Проверить снова" } },
            { "OurServerPage", "Not configured yet", { "Пока не указан" } },
            { "OurServerPage",
              "The server address is not configured yet, the game starts without joining a server.",
              { "Адрес сервера пока не указан, игра запустится без подключения к серверу." } },
            { "OurServerPage", "Checking the server pack...", { "Проверка серверной сборки..." } },
            { "OurServerPage", "⚠ Server pack problem", { "⚠ Проблема с серверной сборкой" } },
            { "OurServerPage", "Try again", { "Повторить" } },
            { "OurServerPage", "✗ Files in the mods folder block the server pack", { "✗ Файлы в папке модов мешают серверной сборке" } },
            { "OurServerPage", "Remove or rename these files: %1", { "Удалите или переименуйте эти файлы: %1" } },
            { "OurServerPage", "✓ Server pack %1", { "✓ Серверная сборка %1" } },
            { "OurServerPage", "All required mods are installed.", { "Все нужные моды установлены." } },
            { "OurServerPage", "✓ Server pack up to date", { "✓ Серверная сборка актуальна" } },
            { "OurServerPage", "🔄 Server pack update available", { "🔄 Доступно обновление серверной сборки" } },
            { "OurServerPage", "%n file(s)", { "%n файл", "%n файла", "%n файлов" } },
            { "OurServerPage", "🔄 Update server pack", { "🔄 Обновить серверную сборку" } },
            { "OurServerPage", "⬇ Mods of the server pack are not installed", { "⬇ Моды серверной сборки не установлены" } },
            { "OurServerPage", "⬇ Install %n mod(s)", { "⬇ Установить %n мод", "⬇ Установить %n мода", "⬇ Установить %n модов" } },
            { "OurServerPage", "✓ Installed", { "✓ Установлен" } },
            { "OurServerPage", "✗ Not installed", { "✗ Не установлен" } },
            { "OurServerPage", "🔄 Update required", { "🔄 Нужно обновление" } },
            { "OurServerPage", "⚠ Damaged, will be downloaded again", { "⚠ Повреждён, будет скачан заново" } },
            { "OurServerPage", "✗ Blocked by another file", { "✗ Мешает другой файл" } },
            { "OurServerPage", "Not selected", { "Не выбран" } },
            { "OurServerPage", "Optional mod, tick it to install it", { "Необязательный мод: поставьте галочку, чтобы установить его" } },
            { "OurServerPage", "Optional mod: %1", { "Необязательный мод: %1" } },
            { "OurServerPage", "Close the game before updating the server pack.", { "Закройте игру перед обновлением серверной сборки." } },
            { "OurServerPage",
              "The server pack could not be checked:\n%1\n\nPlay with the mods that are already installed?",
              { "Не удалось проверить серверную сборку:\n%1\n\nИграть с уже установленными модами?" } },
            { "OurServerPage", "Abort", { "Отмена" } },
            { "OurServerPage", "Players", { "Игроки" } },
            { "OurServerPage", "Ping", { "Пинг" } },
            { "OurServerPage", "Last checked", { "Последняя проверка" } },
            { "OurServerPage", "Players online", { "Игроки на сервере" } },
            { "OurServerPage", "Checking...", { "Проверка..." } },
            { "OurServerPage", "🟢 Online", { "🟢 В сети" } },
            { "OurServerPage", "🔴 Unavailable", { "🔴 Недоступен" } },
            { "OurServerPage", "%1 ms", { "%1 мс" } },
            { "OurServerPage", "No data", { "Нет данных" } },
            { "OurServerPage", "The server does not report its players.", { "Сервер не сообщает, кто на нём играет." } },
            { "OurServerPage", "Nobody is playing right now", { "Сейчас никто не играет" } },
            { "OurServerPage", "…and %n more", { "…и ещё %n", "…и ещё %n", "…и ещё %n" } },
            { "OurServerPage",
              "The server shows only a part of the player list. The full list needs enable-query=true on the server and "
              "\"queryPort\" in the manifest.",
              { "Сервер показывает только часть списка игроков. Для полного списка на сервере нужен enable-query=true, а в "
                "манифесте — \"queryPort\"." } },
            { "OurServerPage", "The full player list could not be requested: %1", { "Не удалось запросить полный список игроков: %1" } },
            { "OurServerPage", "Could not create the server instance: %1", { "Не удалось создать экземпляр для сервера: %1" } },

            // main window and settings
            { "MainWindow", "&Our Server", { "&Наш сервер" } },
            { "MainWindow", "Play on the server of our group", { "Играть на сервере нашей группы" } },
            { "APIPage", "&Our Server", { "&Наш сервер" } },
            { "APIPage", "Server pack manifest URL", { "Адрес манифеста серверной сборки" } },
            { "APIPage", "Server address", { "Адрес сервера" } },
            { "APIPage", "Use the address from the server pack manifest", { "Использовать адрес из манифеста серверной сборки" } },
            { "APIPage",
              "Leave these fields empty to use the defaults. The manifest URL must use HTTPS. The address can include a port, for example "
              "play.example.com:25565.",
              { "Оставьте поля пустыми, чтобы использовать значения по умолчанию. Адрес манифеста должен использовать HTTPS. Адрес "
                "сервера может содержать порт, например play.example.com:25565." } },
        };

        QHash<QString, QStringList> table;
        for (const auto& entry : entries) {
            table.insert(tableKey(entry.context, entry.source), entry.forms);
        }
        return table;
    }();
    return s_table;
}
}  // namespace

OurServerTranslator::OurServerTranslator(QObject* parent) : QTranslator(parent) {}

bool OurServerTranslator::supportsLanguage(const QString& languageCode)
{
    return languageCode == "ru" || languageCode.startsWith("ru_") || languageCode.startsWith("ru-");
}

int OurServerTranslator::russianPluralForm(int n)
{
    n = qAbs(n);
    const int lastDigit = n % 10;
    const int lastTwoDigits = n % 100;
    if (lastDigit == 1 && lastTwoDigits != 11) {
        return 0;
    }
    if (lastDigit >= 2 && lastDigit <= 4 && (lastTwoDigits < 12 || lastTwoDigits > 14)) {
        return 1;
    }
    return 2;
}

QString OurServerTranslator::translate(const char* context,
                                       const char* sourceText,
                                       [[maybe_unused]] const char* disambiguation,
                                       int n) const
{
    if (context == nullptr || sourceText == nullptr) {
        return {};
    }
    const auto& table = translations();
    const auto it = table.constFind(tableKey(context, sourceText));
    if (it == table.constEnd() || it->isEmpty()) {
        return {};
    }
    if (it->size() == 1 || n < 0) {
        return it->first();
    }
    // QCoreApplication::translate replaces %n in the returned text
    return it->at(qMin(russianPluralForm(n), static_cast<int>(it->size()) - 1));
}
