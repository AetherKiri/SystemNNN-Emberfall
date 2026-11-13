package com.tyranor.runtime.systemnnn;

import java.io.IOException;
import java.util.Arrays;

/** Pure JVM checks for the 10-field SaveLayoutConfig contract without loading native libraries. */
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

        System.out.println("PASS "+checks+" PC save layout interop checks");
    }
}
