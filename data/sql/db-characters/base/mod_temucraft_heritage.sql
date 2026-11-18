-- mod-temucraft-heritage: each character's heritage (race 0 = their own) and when it last changed (unix time).
CREATE TABLE IF NOT EXISTS `mod_temucraft_heritage` (
    `guid`    INT UNSIGNED NOT NULL,
    `race`    TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `changed` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- second racials: racials of races that can't be played here, carried on top of the heritage.
CREATE TABLE IF NOT EXISTS `mod_temucraft_heritage_bonus` (
    `guid`  INT UNSIGNED NOT NULL,
    `spell` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
