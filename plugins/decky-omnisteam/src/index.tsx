import {
  definePlugin,
  ServerAPI,
  staticClasses,
  SidebarNavigation,
  PanelSection,
  PanelSectionRow,
  ButtonItem,
  TextField
} from "decky-frontend-lib";
import { VFC, useState, useEffect } from "react";
import { FaGamepad, FaCloudUploadAlt, FaSync } from "react-icons/fa";

interface ScriptInfo {
  fileName: string;
  enabled: boolean;
}

const Content: VFC<{ serverApi: ServerAPI }> = ({ serverApi }) => {
  const [scripts, setScripts] = useState<ScriptInfo[]>([]);
  const [searchQuery, setSearchQuery] = useState<string>("");
  const [statusMsg, setStatusMsg] = useState<string>("Core: Active");

  const fetchScripts = async () => {
    try {
      const res = await fetch("http://127.0.0.1:8080/api/scripts");
      if (res.ok) {
        const data = await res.json();
        setScripts(data);
      }
    } catch (e) {
      setStatusMsg("Manager offline");
    }
  };

  useEffect(() => {
    fetchScripts();
  }, []);

  const handleUnlock = async (appId: number, name: string) => {
    try {
      await fetch("http://127.0.0.1:8080/api/unlock", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ appId, gameName: name })
      });
      fetchScripts();
    } catch (e) {}
  };

  return (
    <PanelSection title="🎮 OmniSteam Controller">
      <PanelSectionRow>
        <div style={{ color: "#38bdf8", fontWeight: "bold" }}>{statusMsg}</div>
      </PanelSectionRow>

      <PanelSection title="📜 Installed Game Unlocks">
        {scripts.map((s) => (
          <PanelSectionRow key={s.fileName}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", width: "100%" }}>
              <span>{s.fileName}</span>
              <span style={{ color: s.enabled ? "#4ade80" : "#94a3b8" }}>
                {s.enabled ? "Active" : "Disabled"}
              </span>
            </div>
          </PanelSectionRow>
        ))}
      </PanelSection>

      <PanelSectionRow>
        <ButtonItem layout="below" onClick={fetchScripts}>
          <FaSync /> Refresh Status
        </ButtonItem>
      </PanelSectionRow>
    </PanelSection>
  );
};

export default definePlugin((serverApi: ServerAPI) => {
  return {
    title: <div className={staticClasses.Title}>OmniSteam</div>,
    content: <Content serverApi={serverApi} />,
    icon: <FaGamepad />,
    onDismount() {}
  };
});
