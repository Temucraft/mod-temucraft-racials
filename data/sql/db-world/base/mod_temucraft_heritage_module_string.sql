-- mod-temucraft-heritage: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-heritage';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-heritage', 1, 'Heritage can''t be changed on this server.'),
('mod-temucraft-heritage', 2, 'You can change your heritage from level {}.'),
('mod-temucraft-heritage', 3, 'Changing your heritage costs {} gold.'),
('mod-temucraft-heritage', 4, 'You changed your heritage recently. Try again in {} h {} min.'),
('mod-temucraft-heritage', 5, 'That heritage isn''t available to you.'),
('mod-temucraft-heritage', 6, 'That heritage isn''t available to you.'),
('mod-temucraft-heritage', 7, '|cffe6cc80Heritage:|r you now carry the racial traits of the {}.'),
('mod-temucraft-heritage', 8, '|cffe6cc80Heritage:|r your own racial traits are back.'),
('mod-temucraft-heritage', 9, 'That is already your heritage.'),
('mod-temucraft-heritage', 10, 'Racial traits you can take on:'),
('mod-temucraft-heritage', 11, '  {} ({}): {}'),
('mod-temucraft-heritage', 12, '|cffe6cc80Heritage:|r {} (level {}+, {} gold, then {} h before the next change).'),
('mod-temucraft-heritage', 13, '.heritage choose <race> | .heritage reset | .heritage bonus'),
('mod-temucraft-heritage', 14, 'That second racial isn''t available.'),
('mod-temucraft-heritage', 15, '|cffe6cc80Heritage:|r you no longer carry {}.'),
('mod-temucraft-heritage', 16, 'You already carry {} second racial(s); drop one first.'),
('mod-temucraft-heritage', 17, '|cffe6cc80Heritage:|r you now also carry {}.'),
('mod-temucraft-heritage', 18, 'Second racials (carry up to {}, {} gold each; .heritage take|drop <name>):'),
('mod-temucraft-heritage', 19, '  {}{} ({})');
