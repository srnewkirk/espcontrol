"use strict";

const { describe, test } = require("node:test");
const { loadTypescriptTest } = require("./helpers/load_typescript_test");

describe("camera motion settings", () => {
  const { runCameraMotionSettingsTests } = loadTypescriptTest("tests/web/camera_motion.test.ts");

  test("normalizes Camera mode, sensitivity and backups", () => {
    runCameraMotionSettingsTests();
  });
});
