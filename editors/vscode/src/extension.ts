import { ExtensionContext, workspace } from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

export function activate(context: ExtensionContext) {
    const config = workspace.getConfiguration("angara.languageServer");
    const angcPath = config.get<string>("path") || "angc";

    const serverOptions: ServerOptions = {
        command: angcPath,
        args: ["lsp"],
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: "file", language: "angara" }],
    };

    client = new LanguageClient(
        "angaraLsp",
        "Angara Language Server",
        serverOptions,
        clientOptions
    );

    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
