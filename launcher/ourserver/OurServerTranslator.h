// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QTranslator>

/**
 * Russian translation of the "Our Server" category.
 *
 * The regular translations are downloaded from Prism Launcher's translation platform, which does not know these strings.
 * This translator is installed on top of them for Russian and only answers for the strings of this feature, everything
 * else falls through to the regular translation files. Plural forms follow the Russian rules (one, few, many).
 */
class OurServerTranslator : public QTranslator {
    Q_OBJECT
   public:
    explicit OurServerTranslator(QObject* parent = nullptr);
    ~OurServerTranslator() override = default;

    static bool supportsLanguage(const QString& languageCode);

    //! 0 = one (1, 21, 101), 1 = few (2-4, 22-24), 2 = many (0, 5-20, 25-30, 111)
    static int russianPluralForm(int n);

    QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override;
    bool isEmpty() const override { return false; }
};
