#include "json.hpp"
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;
struct Result { int rc; std::string out; };
static fs::path root, binary;
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static void text(const fs::path &p, const std::string &s, mode_t mode = 0644) {
    fs::create_directories(p.parent_path()); std::ofstream f(p); f << s; f.close(); CHECK(f.good()); chmod(p.c_str(), mode);
}
static void save(const fs::path &p, const json &v) { text(p, v.dump(2) + "\n"); }
static json load(const fs::path &p) { std::ifstream f(p); json v; f >> v; return v; }
static bool has(const Result &r, const std::string &s) { return r.out.find(s) != std::string::npos; }

static Result call(std::initializer_list<std::string> args) {
    int fd[2]; CHECK(pipe(fd) == 0); pid_t pid = fork(); CHECK(pid >= 0);
    if (!pid) {
        close(fd[0]); dup2(fd[1], 1); dup2(fd[1], 2); close(fd[1]);
        std::vector<std::string> values{binary.string()}; values.insert(values.end(), args);
        std::vector<char *> argv; for (auto &v : values) argv.push_back(v.data()); argv.push_back(nullptr);
        execv(argv[0], argv.data()); _exit(127);
    }
    close(fd[1]); std::string out; char buf[4096]; ssize_t n;
    while ((n = read(fd[0], buf, sizeof(buf))) > 0) out.append(buf, n);
    close(fd[0]);
    int status; CHECK(waitpid(pid, &status, 0) == pid); return {WIFEXITED(status) ? WEXITSTATUS(status) : 255, out};
}
static void call_and_kill(std::initializer_list<std::string> args) {
    pid_t pid=fork(); CHECK(pid>=0);
    if (!pid) {
        std::vector<std::string> values{binary.string()}; values.insert(values.end(),args);
        std::vector<char *> argv; for(auto &v:values) argv.push_back(v.data()); argv.push_back(nullptr);
        execv(argv[0],argv.data()); _exit(127);
    }
    usleep(200000); CHECK(kill(pid,SIGKILL)==0); int status=0; CHECK(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status));
}
static std::string tx(const Result &r) {
    std::istringstream lines(r.out); std::string line;
    while (std::getline(lines, line)) if (line.rfind("PACKAGE_JOB\t", 0) == 0) {
        std::istringstream row(line); std::string f; std::vector<std::string> fields;
        while (std::getline(row, f, '\t')) fields.push_back(f);
        return fields.size() > 5 ? fields[5] : "";
    } return "";
}
static json app(const std::string &id, const std::string &review) {
    return {{"uuid",id},{"share_code",id=="app-id"?"demo":"blocked"},{"title",id=="app-id"?"Demo":"Blocked"},
      {"version","1.2.0"},{"review_status",review},{"featured",id=="app-id"},{"categories",{"Utilities","Tools"}},
      {"i18n",{{"en",{{"title","Demo Local"},{"summary","Localized summary"}}},{"zh-CN",{{"title","演示"},{"summary","本地化摘要"}}}}},{"author",{{"display_name","Tester"}}},
      {"source",{{"repository","https://example.invalid/source"}}},
      {"icon","https://assets.invalid/"+id+".png"},
      {"app",{{"dependencies",{"lib-one","lib-two"}},{"applaunch",{{"desktop_entry","applications/demo.desktop"},{"exec","/usr/bin/demo-package"}}}}},
      {"download",{{"type","deb"},{"package","demo-package"},{"url","https://example.invalid/demo.deb"},{"md5",getenv("FAKE_MD5")},{"size","42K"}}}};
}
static void fixture() {
    fs::create_directories(root/"bin"); fs::create_directories(root/"state"); fs::create_directories(root/"cache"); fs::create_directories(root/"root/applications");
    text(root/"demo.deb", "fake deb payload\n");
    std::string cmd="md5sum '"+(root/"demo.deb").string()+"'"; FILE *p=popen(cmd.c_str(),"r"); char md5[40]={}; CHECK(fgets(md5,sizeof(md5),p)); pclose(p); md5[32]=0; setenv("FAKE_MD5",md5,1);
    save(root/"registry.json", {{"apps",{app("app-id","approved"),app("blocked-id","pending")}}});
    text(root/"bin/curl", R"(#!/bin/sh
set -eu; out=; url=
while [ $# -gt 0 ]; do case "$1" in -o) out=$2;shift 2;; -A|--connect-timeout|--max-time|-C) shift 2;; -*) shift;; *) url=$1;shift;; esac; done
[ "${FAKE_CURL_FAIL:-0}" = 0 ] || exit 22
[ -z "${FAKE_CURL_SLEEP:-}" ] || sleep "$FAKE_CURL_SLEEP"
marker="$M5APPSTORE_CACHE_DIR/network-drop-marker"
if [ "${FAKE_DROP_DURING_ASSETS:-0}" = 1 ] && [ -e "$marker" ]; then exit 7; fi
case "$url" in
  *ipinfo.io*) echo "${FAKE_COUNTRY:-US}";;
  *demo.deb*) cp "$FAKE_DEB" "$out";;
  *.png*) [ "${FAKE_ASSET_FAIL:-0}" = 0 ] || exit 22; printf 'fake icon' > "$out";;
  *) cp "$FAKE_REGISTRY" "$out"; if [ "${FAKE_DROP_DURING_ASSETS:-0}" = 1 ]; then touch "$marker"; fi;;
esac
)",0755);
    text(root/"bin/dpkg-query", R"(#!/bin/sh
set -eu
if [ "$1" = -L ]; then [ "${FAKE_FILES_FAIL:-0}" = 0 ] || exit 2; printf '/usr/bin/demo-package\n/usr/share/APPLaunch/applications/demo.desktop\n'; exit; fi
case "${FAKE_DPKG_MODE:-absent}" in installed) printf '%s\t%s\n' "${FAKE_STATUS:-ii }" "${FAKE_VERSION:-1.2.0}";; un) printf 'un \t0.1.0\n';; error) echo 'database error' >&2;exit 2;; *) echo 'no packages found matching' >&2;exit 1;; esac
)",0755);
    text(root/"bin/dpkg-deb", R"(#!/bin/sh
case "$3" in Package) echo "${FAKE_DEB_PACKAGE:-demo-package}";; Version) echo "${FAKE_DEB_VERSION:-1.2.0}";; Pre-Depends|Depends) echo -n "${FAKE_DEPENDS:-}";; *) exit 1;; esac
)",0755);
    std::string path=(root/"bin").string()+":"+getenv("PATH"); setenv("PATH",path.c_str(),1);
    setenv("FAKE_DEB",(root/"demo.deb").c_str(),1); setenv("FAKE_REGISTRY",(root/"registry.json").c_str(),1);
    setenv("M5APPSTORE_STATE_DIR",(root/"state").c_str(),1); setenv("M5APPSTORE_CACHE_DIR",(root/"cache").c_str(),1);
    setenv("M5APPSTORE_APP_ROOT",(root/"root").c_str(),1); setenv("M5APPSTORE_HTTP_TRANSPORT","curl",1);
    setenv("M5APPSTORE_LOCALE","en",1);
}
static void registry_cases() {
    CHECK(has(call({"--regions"}),"REGION\tauto\tAuto"));
    CHECK(call({"--set-region","moon"}).rc!=0); CHECK(call({"--set-region","CN"}).rc==0);
    CHECK(has(call({"--registry-config"}),"CONFIG\tCN\tCN\t1")); CHECK(call({"--set-region","default"}).rc==0);
    Result s=call({"--sync"}); CHECK(s.rc==0&&has(s,"SYNC\t1\t0\t0\t2"));
    Result sum=call({"--summary"}); CHECK(has(sum,"META\t1\t2 apps/1 registries")); CHECK(has(sum,"CAT\tUtilities"));
    CHECK(has(sum,"APP\tapp-id\tDemo Local\t1.2.0\tUtilities")); CHECK(has(sum,"\tTester\thttps://example.invalid/source\t"));
    CHECK(call({"--clear-registry-cache"}).rc==0);
    setenv("FAKE_DROP_DURING_ASSETS","1",1);
    Result dropped=call({"--sync"});
    CHECK(has(dropped,"SYNC\t0\t0\t1\t2\tNo registry could be reached"));
    unsetenv("FAKE_DROP_DURING_ASSETS");
    fs::remove(root/"cache/network-drop-marker");
    CHECK(has(call({"--sync"}),"SYNC\t1\t0\t0\t2"));
    CHECK(call({"--clear-registry-cache"}).rc==0);
    setenv("FAKE_ASSET_FAIL","1",1);
    CHECK(has(call({"--sync"}),"SYNC\t1\t0\t0\t2\tCatalog synced"));
    unsetenv("FAKE_ASSET_FAIL");
    CHECK(has(call({"--sync"}),"SYNC\t1\t0\t0\t2"));
    setenv("M5APPSTORE_LOCALE","zh_CN.UTF-8",1); CHECK(has(call({"--summary"}),"APP\tapp-id\t演示")); setenv("M5APPSTORE_LOCALE","en",1);
    CHECK(has(call({"--registries"}),"\tok\t2\t")); setenv("FAKE_CURL_FAIL","1",1);
    CHECK(has(call({"--sync"}),"SYNC\t0\t1\t0\t2")); CHECK(has(call({"--summary"}),"WARN\tRegistry offline")); unsetenv("FAKE_CURL_FAIL");
    CHECK(call({"--add-registry","example.invalid/registry.json","--registry-name","Extra"}).rc==0);
    CHECK(call({"--add-registry","example.invalid/registry.json"}).rc!=0); CHECK(call({"--disable-registry","https://example.invalid/registry.json"}).rc==0);
    CHECK(has(call({"--registry-config"}),"Extra\thttps://example.invalid/registry.json\t0"));
    CHECK(call({"--enable-registry","https://example.invalid/registry.json"}).rc==0);
    CHECK(call({"--edit-registry","https://example.invalid/registry.json","https://new.invalid/registry.json","--registry-name","New"}).rc==0);
    CHECK(call({"--remove-registry","https://new.invalid/registry.json"}).rc==0); CHECK(call({"--remove-registry","https://missing.invalid/x"}).rc!=0);
    CHECK(call({"--remove-registry","https://cardputerzero.github.io/generated/registry.json"}).rc!=0);
    json cfg={{"region","default"},{"active_region","default"},{"registries",{{{"name","Custom"},{"url","https://custom.invalid/registry.json"},{"enabled",false}}}}};
    CHECK(has(call({"--replace-registry-config",cfg.dump()}),"CONFIG_REG\t1\tCustom")); CHECK(call({"--replace-registry-config","{"}).rc!=0);
    fs::path c=root/"state/registries.json"; chmod(c.c_str(),0640); CHECK(call({"--set-region","CN"}).rc==0); struct stat st{}; CHECK(!stat(c.c_str(),&st)&&(st.st_mode&0777)==0640);
    CHECK(call({"--set-region","default"}).rc==0); CHECK(call({"--sync"}).rc==0);
}
static void plan_cases() {
    Result p=call({"--plan","demo"}); CHECK(p.rc==0&&has(p,"PLAN\tapp-id\tDemo Local\t1.2.0\t42K")); CHECK(has(p,"lib-one,lib-two"));
    CHECK(call({"--plan","blocked"}).rc!=0); CHECK(call({"--plan","missing"}).rc!=0);
}
static void complete(const fs::path &p) { json v=load(p);v["helper_completed"]=true;save(p,v); }
static json pending_job(const std::string &id, bool previously_installed=false, const std::string &previous_version="") {
    return {{"schema_version",2},{"transaction_id",id},{"action","install"},{"app_id","app-id"},
      {"package","demo-package"},{"previously_installed",previously_installed},{"previous_version",previous_version},
      {"expected_package_version","1.2.0"},{"deb_path",(root/"cache/downloads/cached-demo.deb").string()},
      {"helper_completed",false},{"app_snapshot",app("app-id","approved")}};
}
static void package_cases() {
    fs::path pending=root/"state/pending-package.json", installed=root/"state/installed.json";
    setenv("FAKE_DPKG_MODE","absent",1); Result prep=call({"--prepare-package","install","demo"}); std::string id=tx(prep);
    CHECK(prep.rc==0&&!id.empty()&&fs::exists(pending)); json j=load(pending); CHECK(j["previously_installed"]==false&&j["expected_package_version"]=="1.2.0");
    CHECK(call({"--repair-package-transaction","blocked-id"}).rc!=0&&fs::exists(pending));
    Result repaired=call({"--repair-package-transaction","app-id"});
    CHECK(repaired.rc==0&&has(repaired,"PACKAGE_REPAIRED\tapp-id")&&!fs::exists(pending));
    fs::create_directories(root/"cache/downloads");
    fs::copy_file(root/"demo.deb", root/"cache/downloads/cached-demo.deb",
                  fs::copy_options::overwrite_existing);
    save(pending, pending_job("conflict"));
    setenv("FAKE_DPKG_MODE","installed",1); setenv("FAKE_VERSION","9.9.0",1);
    Result conflict=call({"--prepare-package","install","blocked"});
    CHECK(conflict.rc!=0&&has(conflict,"PENDING_CONFLICT\tapp-id\tinstall\tdemo-package"));
    repaired=call({"--repair-package-transaction","app-id"});
    CHECK(repaired.rc==0&&has(repaired,"prepared for retry")&&fs::exists(pending));
    CHECK(!load(pending).value("helper_completed",true));
    Result recovered_retry=call({"--prepare-package","install","demo"});
    CHECK(recovered_retry.rc==0&&tx(recovered_retry)=="conflict");
    fs::remove(pending); unsetenv("FAKE_VERSION"); setenv("FAKE_DPKG_MODE","absent",1);
    prep=call({"--prepare-package","install","demo"}); id=tx(prep);
    CHECK(prep.rc==0&&!id.empty()&&fs::exists(pending));
    Result retry=call({"--prepare-package","install","demo"}); CHECK(retry.rc==0&&tx(retry)==id);
    CHECK(call({"--finalize-package","install","demo","wrong"}).rc!=0);
    CHECK(call({"--finalize-package","install","demo",id}).rc!=0&&fs::exists(pending)); complete(pending);
    setenv("FAKE_DPKG_MODE","installed",1); Result done=call({"--finalize-package","install","demo",id}); CHECK(done.rc==0&&has(done,"PACKAGE_RESULT\tinstall\tapp-id\tdemo-package\t1.2.0"));
    CHECK(!fs::exists(pending)&&load(installed)["app-id"]["files"].size()==2); CHECK(call({"--finalize-package","install","demo",id}).rc==0);
    Result un=call({"--prepare-package","uninstall","demo"}); std::string uid=tx(un); CHECK(un.rc==0&&!uid.empty()); complete(pending);setenv("FAKE_DPKG_MODE","absent",1);
    CHECK(call({"--finalize-package","uninstall","demo",uid}).rc==0); CHECK(!load(installed).contains("app-id")); CHECK(call({"--finalize-package","uninstall","demo",uid}).rc==0);
    fs::copy_file(root/"demo.deb",root/"cache/downloads/cached-demo.deb",fs::copy_options::overwrite_existing);
    save(pending,pending_job("idle"));
    CHECK(call({"--summary"}).rc==0&&fs::exists(pending));
    CHECK(tx(call({"--prepare-package","install","demo"}))=="idle"); fs::remove(pending);
    save(pending,pending_job("unknown-query")); setenv("FAKE_DPKG_MODE","error",1);
    CHECK(call({"--summary"}).rc==0&&fs::exists(pending)); fs::remove(pending);
    save(pending,pending_job("un-status")); setenv("FAKE_DPKG_MODE","un",1);
    CHECK(call({"--summary"}).rc==0&&fs::exists(pending)); fs::remove(pending);
    save(pending,pending_job("rc-status")); setenv("FAKE_DPKG_MODE","installed",1); setenv("FAKE_STATUS","rc ",1);
    CHECK(call({"--summary"}).rc==0&&fs::exists(pending)); fs::remove(pending);
    save(pending,pending_job("ri-status",true,"1.2.0")); setenv("FAKE_STATUS","ri ",1);
    CHECK(call({"--summary"}).rc==0&&fs::exists(pending)); fs::remove(pending);
    save(pending,pending_job("unknown-status")); setenv("FAKE_STATUS","xx ",1);
    CHECK(call({"--summary"}).rc==0&&fs::exists(pending)); fs::remove(pending); unsetenv("FAKE_STATUS");
    save(pending,{{"schema_version",2},{"transaction_id","applied"},{"action","install"},{"app_id","app-id"},{"package","demo-package"},{"previously_installed",false},{"previous_version",""},{"expected_package_version","1.2.0"},{"helper_completed",false},{"app_snapshot",app("app-id","approved")}});
    setenv("FAKE_DPKG_MODE","installed",1); CHECK(call({"--summary"}).rc==0&&!fs::exists(pending)&&load(installed).contains("app-id"));
    save(pending,{{"schema_version",2},{"transaction_id","applied-uninstall"},{"action","uninstall"},{"app_id","app-id"},{"package","demo-package"},{"previously_installed",true},{"previous_version","1.2.0"},{"helper_completed",false},{"app_snapshot",app("app-id","approved")}});
    setenv("FAKE_DPKG_MODE","absent",1); CHECK(call({"--summary"}).rc==0&&!fs::exists(pending)&&!load(installed).contains("app-id"));
    save(pending,{{"schema_version",2},{"transaction_id","partial-uninstall"},{"action","uninstall"},{"app_id","app-id"},{"package","demo-package"},{"previously_installed",true},{"previous_version","1.2.0"},{"helper_completed",false},{"app_snapshot",app("app-id","approved")}});
    setenv("FAKE_DPKG_MODE","error",1); Result partial_summary=call({"--summary"});
    CHECK(partial_summary.rc==0&&has(partial_summary,"APP\tapp-id\tDemo Local\t1.2.0\tUtilities\t1"));
    CHECK(has(partial_summary,"WARN\tInterrupted uninstall pending"));
    Result retry_uninstall=call({"--prepare-package","uninstall","demo"});
    CHECK(retry_uninstall.rc==0&&tx(retry_uninstall)=="partial-uninstall"); fs::remove(pending);
    text(pending,"{broken json\n"); CHECK(call({"--summary"}).rc==0&&!fs::exists(pending));
    setenv("FAKE_DPKG_MODE","installed",1); Result reinstall=call({"--prepare-package","reinstall","demo"});
    CHECK(reinstall.rc==0&&!tx(reinstall).empty()&&has(reinstall,"PACKAGE_JOB\tinstall")&&has(reinstall,"\t1\t"));
    Result reinstall_retry=call({"--prepare-package","reinstall","demo"}); CHECK(tx(reinstall_retry)==tx(reinstall)); fs::remove(pending);
    setenv("FAKE_DPKG_MODE","absent",1);
    CHECK(call({"--prepare-package","uninstall","demo"}).rc!=0);
    CHECK(call({"--prepare-package","reinstall","demo"}).rc!=0);
    CHECK(call({"--prepare-package","upgrade","demo"}).rc!=0);
    setenv("FAKE_DPKG_MODE","installed",1);
    CHECK(call({"--prepare-package","install","demo"}).rc!=0);
    save(pending,{{"schema_version",2},{"transaction_id","badver"},{"action","upgrade"},{"app_id","app-id"},{"package","demo-package"},{"previously_installed",true},{"previous_version","1.1.0"},{"expected_package_version","1.2.0"},{"helper_completed",true},{"app_snapshot",app("app-id","approved")}});
    setenv("FAKE_VERSION","1.1.0",1); CHECK(call({"--finalize-package","upgrade","demo","badver"}).rc!=0&&fs::exists(pending));
    CHECK(call({"--summary"}).rc==0&&!load(pending).value("helper_completed",true));
    CHECK(load(pending).value("recovery_reason","")=="package state changed before finalization");
    fs::remove(pending); unsetenv("FAKE_VERSION");
    save(pending,pending_job("files-fail")); complete(pending); setenv("FAKE_FILES_FAIL","1",1);
    CHECK(call({"--finalize-package","install","demo","files-fail"}).rc!=0&&fs::exists(pending)); fs::remove(pending); unsetenv("FAKE_FILES_FAIL");
    setenv("FAKE_DEB_PACKAGE","wrong",1); fs::remove_all(root/"cache/downloads"); CHECK(call({"--prepare-package","install","demo"}).rc!=0&&!fs::exists(pending)); unsetenv("FAKE_DEB_PACKAGE");
    fs::remove_all(root/"cache/downloads"); setenv("FAKE_CURL_SLEEP","3",1);
    setenv("FAKE_DPKG_MODE","absent",1); call_and_kill({"--prepare-package","install","demo"});
    unsetenv("FAKE_CURL_SLEEP"); CHECK(!fs::exists(pending));
    Result after_kill=call({"--prepare-package","install","demo"}); CHECK(after_kill.rc==0&&!tx(after_kill).empty());
    int ready[2]; CHECK(pipe(ready)==0); pid_t holder=fork(); CHECK(holder>=0);
    if (!holder) { close(ready[0]); int lockfd=open((root/"state/package-transaction.lock").c_str(),O_RDWR|O_CREAT,0644); if(lockfd<0||flock(lockfd,LOCK_EX)!=0)_exit(2); write(ready[1],"x",1); sleep(5); _exit(0); }
    close(ready[1]); char marker=0; CHECK(read(ready[0],&marker,1)==1); close(ready[0]);
    setenv("M5APPSTORE_LOCK_TIMEOUT","1",1); Result busy=call({"--prepare-package","install","demo"});
    CHECK(busy.rc!=0&&has(busy,"package transaction is still busy")); unsetenv("M5APPSTORE_LOCK_TIMEOUT");
    kill(holder,SIGTERM); int holder_status=0; CHECK(waitpid(holder,&holder_status,0)==holder); fs::remove(pending);
    CHECK(call({"--install","demo"}).rc!=0); CHECK(has(call({"--package-helper","install"}),"package helper requires root"));
}
int main(int argc,char **argv) {
    CHECK(argc==2); binary=fs::absolute(argv[1]); char t[]="/tmp/appstore-cpp-parity.XXXXXX"; char *p=mkdtemp(t);CHECK(p);root=p;
    fixture();registry_cases();plan_cases();package_cases();fs::remove_all(root);std::cout<<"native C++ parity tests passed: "<<checks<<" checks\n";
}
