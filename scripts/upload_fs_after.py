Import("env")

def upload_fs(source, target, env):
    env.Execute("pio run --target uploadfs --environment " + env["PIOENV"])

env.AddPostAction("upload", upload_fs)
