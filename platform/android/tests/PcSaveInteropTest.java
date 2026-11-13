package com.tyranor.runtime.systemnnn;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.time.LocalDate;
import java.util.Arrays;

/** Pure JVM checks for SaveLayoutConfig and PC .sav GAMEVAR / gameDate extraction without loading native libraries. */
public final class PcSaveInteropTest {
    private static int checks;

    private static void check(boolean condition,String message){
        if(!condition)throw new AssertionError(message);
        checks++;
    }

    private interface Action { void run() throws Exception; }

    private static void rejects(Action action,String reason) throws Exception {
        try{
            action.run();
            throw new AssertionError("Accepted invalid layout: "+reason);
        }catch(IOException expected){
            checks++;
        }
    }

    public static void main(String[] args) throws Exception {
        String cfgPath="/game/init2/emberfall-system.cfg";
        int[] executable={32,1024,7088,3,1,1,1,1,0,1};
        SaveLayoutConfig parsed=SaveLayoutConfig.fromFields(cfgPath,executable);
        check(parsed.path.equals(cfgPath),"layout path preserved");
        check(parsed.layerRecords==32&&parsed.messageWidth==1024&&parsed.statusBytes==7088,"story geometry fields");
        check(parsed.systemSaveVersion==3&&parsed.systemSaveSource==SaveLayoutConfig.SystemSaveSource.EXECUTABLE,
            "executable system save version and source");
        check(parsed.executableLayout&&parsed.okikae,"executable layout and okikae flags");
        check(parsed.executableMessageWidth&&!parsed.executableStatusBytes&&parsed.executableOkikae,
            "per-field executable provenance flags");
        check(Arrays.equals(parsed.fields(),executable),"canonical 10-field array round-trip");
        SaveLayoutConfig roundTrip=SaveLayoutConfig.fromFields(parsed.path,parsed.fields());
        check(Arrays.equals(roundTrip.fields(),executable),"re-parsed 10-field array round-trip");

        int[] pending={16,256,7088,0,0,0,0,0,1,0};
        SaveLayoutConfig pendingCfg=SaveLayoutConfig.fromFields(cfgPath,pending);
        check(pendingCfg.systemSaveSource==SaveLayoutConfig.SystemSaveSource.PENDING&&pendingCfg.systemSaveVersion==0,
            "pending source requires version 0");
        check(!pendingCfg.executableLayout&&!pendingCfg.okikae&&!pendingCfg.executableMessageWidth&&
            pendingCfg.executableStatusBytes&&!pendingCfg.executableOkikae,"pending boolean flags");
        check(Arrays.equals(pendingCfg.fields(),pending),"pending 10-field round-trip");

        for(int source=1;source<=3;source++){
            for(int version=1;version<=3;version++){
                int[] nonCanonical={16,256,8112,version,2,-3,source,7,0,-1};
                SaveLayoutConfig normalized=SaveLayoutConfig.fromFields(cfgPath,nonCanonical);
                check(normalized.systemSaveSource.ordinal()==source&&normalized.systemSaveVersion==version,
                    "source "+source+" version "+version);
                check(Arrays.equals(normalized.fields(),new int[]{16,256,8112,version,1,1,source,1,0,1}),
                    "fields() normalizes nonzero boolean ints to 1");
            }
        }

        rejects(()->SaveLayoutConfig.fromFields(null,executable),"null path");
        rejects(()->SaveLayoutConfig.fromFields("",executable),"empty path");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,null),"null fields");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,3,1,1,1}),"legacy 7-field array");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[9]),"short 9-field array");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[11]),"long 11-field array");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,1,1,1,-1,1,0,1}),"negative source");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,1,1,1,4,1,0,1}),"out-of-range source");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,1,1,1,0,1,0,1}),"pending source with nonzero version");
        rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,-1,1,1,0,1,0,1}),"pending source with negative version");
        for(int source=1;source<=3;source++){
            final int s=source;
            rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,0,1,1,s,1,0,1}),"resolved source "+s+" with version 0");
            rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,4,1,1,s,1,0,1}),"resolved source "+s+" with version 4");
            rejects(()->SaveLayoutConfig.fromFields(cfgPath,new int[]{32,1024,7088,-1,1,1,s,1,0,1}),"resolved source "+s+" with negative version");
        }

        verifyCalcuWeekFormula();
        verifyVarLookupAndGameVarExtraction();

        System.out.println("PASS "+checks+" PC save layout interop checks");
    }

    private static void verifyCalcuWeekFormula() {
        for(LocalDate d=LocalDate.of(1970,1,1);!d.isAfter(LocalDate.of(2040,12,31));d=d.plusDays(1)){
            int expected=d.getDayOfWeek().getValue()%7; // Sun=0, Mon=1 .. Sat=6
            int actual=PcStorySave.calcuWeek(d.getYear(),d.getMonthValue(),d.getDayOfMonth());
            if(actual!=expected){
                throw new AssertionError("calcuWeek mismatch at "+d+": expected "+expected+" got "+actual);
            }
        }
        checks++;
        check(PcStorySave.calcuWeek(2000,1,1)==6,"2000-01-01 is Saturday (6)");
        check(PcStorySave.calcuWeek(2000,2,29)==2,"2000-02-29 leap day is Tuesday (2)");
        check(PcStorySave.calcuWeek(1900,3,1)==4,"1900-03-01 non-leap century is Thursday (4)");
        check(PcStorySave.calcuWeek(2026,11,13)==5,"2026-11-13 is Friday (5)");
        check(PcStorySave.calcuWeek(0,1,1)==0,"year=0 date=0101 computes Sunday (0)");
        check(PcStorySave.calcuWeek(-40,1,1)==6,"negative raw sum (-15) normalizes modulo to 6 matching C++ weekday_ymd");
        check(PcStorySave.calcuWeek(-100,1,1)==2,"negative century year (-100, raw -89) normalizes modulo to 2 matching C++ weekday_ymd");
        for(int y=-500;y<=10;y++){
            int w=PcStorySave.calcuWeek(y,0,0);
            if(w<0||w>6)throw new AssertionError("calcuWeek out of 0..6 for year="+y+": "+w);
        }
        checks++;
        check(PcStorySave.calcuWeek(Integer.MIN_VALUE,1,1)>=0&&PcStorySave.calcuWeek(Integer.MIN_VALUE,1,1)<=6,
            "Integer.MIN_VALUE year does not overflow or return negative weekday");
    }

    private static byte[] block(int code,String name,byte[] payload){
        ByteBuffer buf=ByteBuffer.allocate(32+payload.length).order(ByteOrder.LITTLE_ENDIAN);
        buf.putInt(32+payload.length);
        buf.putInt(code);
        buf.putInt(0);
        buf.putInt(0);
        byte[] ascii=name.getBytes(StandardCharsets.ISO_8859_1);
        buf.position(16);
        buf.put(ascii,0,Math.min(15,ascii.length));
        buf.position(32);
        buf.put(payload);
        return buf.array();
    }

    private static byte[] syntheticPcSave(int varCount,int yearIdx,int yearVal,int dateIdx,int dateVal){
        ByteBuffer vars=ByteBuffer.allocate(varCount*4).order(ByteOrder.LITTLE_ENDIAN);
        if(yearIdx>=0&&yearIdx<varCount)vars.putInt(yearIdx*4,yearVal);
        if(dateIdx>=0&&dateIdx<varCount)vars.putInt(dateIdx*4,dateVal);
        try{
            ByteArrayOutputStream out=new ByteArrayOutputStream();
            out.write(block(0,"SAVEDATA",new byte[0]));
            out.write(block(1,"INFO",new byte[32]));
            out.write(block(4,"GAMEVAR",vars.array()));
            return out.toByteArray();
        }catch(IOException impossible){
            throw new AssertionError(impossible);
        }
    }

    private static void writeVarFile(File file,String text,boolean encrypted) throws IOException {
        file.getParentFile().mkdirs();
        byte[] raw=text.getBytes(StandardCharsets.ISO_8859_1);
        if(encrypted)for(int i=0;i<raw.length;i++)raw[i]^=(byte)0xff;
        Files.write(file.toPath(),raw);
    }

    private static void removeTree(File file){
        if(file.isDirectory()){
            File[] children=file.listFiles();
            if(children!=null)for(File child:children)removeTree(child);
        }
        file.delete();
    }

    private static void verifyVarLookupAndGameVarExtraction() throws Exception {
        File root=Files.createTempDirectory("nnn-gamedate-vars-").toFile();
        try{
            check(!PcStorySave.GameDateVars.load(root,null).enabled(),"missing spt directory yields NONE");
            writeVarFile(new File(root,"spt/var.fxf"),"rnd,date,time,holiday,week,\r\nv5,v6,v7,v8,v9,YEAR,v11\r\n",true);
            PcStorySave.GameDateVars classic=PcStorySave.GameDateVars.load(root,null);
            check(classic.enabled()&&classic.dateIndex==1&&classic.yearIndex==10,
                "FXF XOR variable list parses trailing comma, multi-line slots and case-insensitive names");

            StringBuilder extVars=new StringBuilder();
            for(int i=0;i<1205;i++)extVars.append(i==1201?"date":i==1204?"year":"var"+i).append(',');
            writeVarFile(new File(root,"spt/var2.fxf"),extVars.toString(),true);
            PcStorySave.GameDateVars extended=PcStorySave.GameDateVars.load(root,null);
            check(extended.dateIndex==1201&&extended.yearIndex==1204,"default varType=1 prefers spt/var2.fxf");

            writeVarFile(new File(root,"init/game.xtx"),"varType 0\n",false);
            GameUiAssets.Config classicCfg=GameUiAssets.readConfig(root,"game");
            PcStorySave.GameDateVars selectedClassic=PcStorySave.GameDateVars.load(root,classicCfg);
            check(selectedClassic.dateIndex==1&&selectedClassic.yearIndex==10,"varType=0 selects spt/var.fxf");

            writeVarFile(new File(root,"init/game.xtx"),"varType 0\nnotUseYearVar 1\n",false);
            PcStorySave.GameDateVars noYear=PcStorySave.GameDateVars.load(root,GameUiAssets.readConfig(root,"game"));
            check(noYear.enabled()&&noYear.yearIndex==-1&&noYear.dateIndex==1,"notUseYearVar=1 suppresses year lookup");

            byte[] save1024=syntheticPcSave(1024,10,2026,1,1113);
            check(Integer.valueOf(2026).equals(PcStorySave.readGameVar(save1024,10))&&
                Integer.valueOf(1113).equals(PcStorySave.readGameVar(save1024,1))&&
                PcStorySave.readGameVar(save1024,1024)==null&&PcStorySave.readGameVar(save1024,-1)==null,
                "classic 1024-slot GAMEVAR bounds and values");
            PcStorySave.GameDate gd=PcStorySave.readGameDate(save1024,classic);
            check(gd!=null&&gd.year==2026&&gd.date==1113&&gd.month==11&&gd.day==13&&gd.week==5,
                "classic GAMEVAR extracts year/month/day/week");
            PcStorySave.GameDate gdDefaultYear=PcStorySave.readGameDate(save1024,noYear);
            check(gdDefaultYear!=null&&gdDefaultYear.year==2000&&gdDefaultYear.month==11&&gdDefaultYear.day==13&&
                gdDefaultYear.week==PcStorySave.calcuWeek(2000,11,13),
                "omitted year variable defaults to 2000 matching CPrintGameDate/CGameCallBack");
            PcStorySave.GameDateVars yearOnly=new PcStorySave.GameDateVars(10,-1);
            PcStorySave.GameDate gdYearOnly=PcStorySave.readGameDate(save1024,yearOnly);
            check(gdYearOnly!=null&&gdYearOnly.year==2026&&gdYearOnly.date==0&&gdYearOnly.month==0&&gdYearOnly.day==0&&
                gdYearOnly.week==PcStorySave.calcuWeek(2026,0,0),
                "year-only variable without date variable defaults date=0 matching C++ resolve_game_date");
            check(PcStorySave.readGameDate(save1024,extended)==null,
                "extended variable indices beyond 1024 are rejected on classic GAMEVAR block");

            byte[] save2224=syntheticPcSave(2224,1204,2024,1201,229);
            PcStorySave.GameDate gdExt=PcStorySave.readGameDate(save2224,extended);
            check(gdExt!=null&&gdExt.year==2024&&gdExt.month==2&&gdExt.day==29&&gdExt.week==4,
                "extended 2224-slot GAMEVAR extracts leap-day Thursday (4)");

            byte[] badHeader=save1024.clone();
            badHeader[16]='X';
            check(PcStorySave.readGameDate(badHeader,classic)==null,"wrong block-0 SAVEDATA name rejected");
            check(PcStorySave.readGameDate(Arrays.copyOf(save1024,save1024.length-1),classic)==null,
                "truncated block rejected");
            ByteArrayOutputStream badVarSize=new ByteArrayOutputStream();
            badVarSize.write(block(0,"SAVEDATA",new byte[0]));
            badVarSize.write(block(4,"GAMEVAR",new byte[100]));
            check(PcStorySave.readGameDate(badVarSize.toByteArray(),classic)==null,
                "unsupported GAMEVAR payload size rejected");

            int[] inspectVars1024=new int[1024];
            inspectVars1024[10]=2026;
            inspectVars1024[1]=1113;
            PcStorySave.GameDate fromInspect=PcStorySave.readGameDate(inspectVars1024,classic);
            check(fromInspect!=null&&fromInspect.year==2026&&fromInspect.month==11&&fromInspect.day==13&&fromInspect.week==5,
                "inspectPcSave data[3] GAMEVAR array resolves year/date/week directly");
            check(PcStorySave.readGameDate(inspectVars1024,extended)==null,
                "inspectPcSave 1024-var array rejects 2224-var index");

            File initFallbackRoot=new File(root,"init-var-fallback");
            writeVarFile(new File(initFallbackRoot,"init/var.fxf"),"v0,date,v2,year\r\n",true);
            PcStorySave.GameDateVars initFallback=PcStorySave.GameDateVars.load(initFallbackRoot,null);
            check(initFallback.dateIndex==1&&initFallback.yearIndex==3,
                "var.fxf lookup falls back to init/ when spt/ is absent");

            Files.write(new File(root,"spt/var2.fxf").toPath(),new byte[1024*1024+1]);
            rejects(()->PcStorySave.GameDateVars.load(root,null),"oversize var2.fxf");
        }finally{
            removeTree(root);
        }

        String envDesktop=System.getenv("SYSTEMNNN_DESKTOP");
        for(String candidate:new String[]{envDesktop,"/mnt/c/Users/a7188/Desktop","C:\\Users\\a7188\\Desktop"}){
            if(candidate==null)continue;
            File desktop=new File(candidate);
            if(!desktop.isDirectory())continue;
            File mushiai=new File(desktop,"虫爱少女 Ver1.10");
            File sav=new File(mushiai,"sav/data0.sav");
            if(sav.isFile()){
                GameUiAssets.Config gm=GameUiAssets.readConfig(mushiai,"game");
                PcStorySave.GameDateVars realVars=PcStorySave.GameDateVars.load(mushiai,gm);
                check(realVars.yearIndex==10&&realVars.dateIndex==1,"real Mushiai var.fxf year=10 date=1");
                byte[] realBytes=Files.readAllBytes(sav.toPath());
                PcStorySave.GameDate realDate=PcStorySave.readGameDate(realBytes,realVars);
                check(realDate!=null&&realDate.year==2000&&realDate.date==101&&realDate.month==1&&
                    realDate.day==1&&realDate.week==6,"real Mushiai data0.sav GAMEVAR 2000-01-01 Saturday");
                break;
            }
        }
    }
}
