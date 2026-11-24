#include "ChemistrySymbols.h"

const QVector<ChemistrySection>& chemistrySections()
{
    static const QVector<ChemistrySection> sections = {
        { QStringLiteral("Main"), {
            { QStringLiteral("common-h2o"), QStringLiteral("H\u2082O"), QStringLiteral("\\ce{\u00ABH2O\u00BB}"), QStringLiteral("Water"), true },
            { QStringLiteral("formula-co2"), QStringLiteral("CO\u2082"), QStringLiteral("\\ce{\u00ABCO2\u00BB}"), QStringLiteral("Carbon dioxide"), true },
            { QStringLiteral("formula-naoh"), QStringLiteral("NaOH"), QStringLiteral("\\ce{\u00ABNaOH\u00BB}"), QStringLiteral("Sodium hydroxide"), true },
            { QStringLiteral("formula-h2so4"), QStringLiteral("H\u2082SO\u2084"), QStringLiteral("\\ce{\u00ABH2SO4\u00BB}"), QStringLiteral("Sulfuric acid"), true },
            { QStringLiteral("common-hplus"), QStringLiteral("H\u207A"), QStringLiteral("\\ce{\u00ABH\u00BB+}"), QStringLiteral("Cation"), true },
            { QStringLiteral("ion-ohminus"), QStringLiteral("OH\u207B"), QStringLiteral("\\ce{\u00ABOH\u00BB-}"), QStringLiteral("Hydroxide anion"), true },
            { QStringLiteral("ion-ca2plus"), QStringLiteral("Ca\u00B2\u207A"), QStringLiteral("\\ce{\u00ABCa\u00BB^2+}"), QStringLiteral("Calcium cation"), true },
            { QStringLiteral("ion-so4"), QStringLiteral("SO\u2084\u00B2\u207B"), QStringLiteral("\\ce{\u00ABSO4\u00BB^2-}"), QStringLiteral("Sulfate anion"), true },
            { QStringLiteral("ion-agcl2"), QStringLiteral("[AgCl\u2082]\u207B"), QStringLiteral("\\ce{[\u00ABAgCl2\u00BB]-}"), QStringLiteral("Complex anion"), true },
            { QStringLiteral("isotope-c"), QStringLiteral("\u00B9\u2074\u2086C"), QStringLiteral("\\ce{^{14}_{6}\u00ABC\u00BB}"), QStringLiteral("Carbon-14"), true },
        } },
        { QStringLiteral("Arrows"), {
            { QStringLiteral("arrow-right"), QStringLiteral("A \u2192 B"), QStringLiteral("\\ce{\u00ABA\u00BB -> B}"), QStringLiteral("Reacts to form"), true },
            { QStringLiteral("arrow-equil"), QStringLiteral("A \u21CC B"), QStringLiteral("\\ce{\u00ABA\u00BB <=> B}"), QStringLiteral("Equilibrium"), true },
            { QStringLiteral("arrow-double"), QStringLiteral("A \u2194 B"), QStringLiteral("\\ce{\u00ABA\u00BB <-> B}"), QStringLiteral("Resonance structures"), true },
            { QStringLiteral("arrow-cond"), QStringLiteral("A \u2192(H\u2082O) B"), QStringLiteral("\\ce{\u00ABA\u00BB ->[H2O] B}"), QStringLiteral("Arrow with condition above"), false },
            { QStringLiteral("arrow-cond2"), QStringLiteral("A \u2192(above / below) B"), QStringLiteral("\\ce{\u00ABA\u00BB ->[text above][text below] B}"), QStringLiteral("Arrow with conditions above and below"), false },
        } },
        { QStringLiteral("States"), {
            { QStringLiteral("state-gas"), QStringLiteral("H\u2082(g)"), QStringLiteral("\\ce{\u00ABH2\u00BB(g)}"), QStringLiteral("Gas"), true },
            { QStringLiteral("state-liquid"), QStringLiteral("H\u2082O(l)"), QStringLiteral("\\ce{\u00ABH2O\u00BB(l)}"), QStringLiteral("Liquid"), true },
            { QStringLiteral("state-solid"), QStringLiteral("NaCl(s)"), QStringLiteral("\\ce{\u00ABNaCl\u00BB(s)}"), QStringLiteral("Solid"), true },
            { QStringLiteral("state-aq"), QStringLiteral("NaOH(aq)"), QStringLiteral("\\ce{\u00ABNaOH\u00BB(aq)}"), QStringLiteral("Aqueous solution"), true },
        } },
    };
    return sections;
}
