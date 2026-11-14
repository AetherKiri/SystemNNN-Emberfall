package com.tyranor.runtime.systemnnn;

import java.util.Arrays;

public final class ChartViewTest {
    private static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static ChartView chart(Integer minimum){
        int[] geometry=new int[minimum==null?27:29];
        geometry[0]=minimum==null?0:minimum;geometry[1]=minimum==null?6700:7000;
        geometry[2]=7300;geometry[13]=300;geometry[20]=1;geometry[25]=7300;
        if(minimum!=null)geometry[27]=minimum;
        byte[][] text=new byte[5][];Arrays.fill(text,new byte[0]);
        return new ChartView(geometry,text,raw->"");
    }
    public static void main(String[] args){
        ChartView modern=chart(null);
        check(modern.minScroll==0&&modern.clampScroll(-1)==0,"existing chart payload keeps its zero lower bound");
        check(modern.miniLength(600)==24&&modern.miniOffset(6700,600)==275,"modern minimap geometry stays unchanged");
        ChartView legacy=chart(-300);
        check(legacy.clampScroll(-301)==-300&&legacy.clampScroll(-1)==-1&&legacy.clampScroll(7001)==7000,
            "negative scrolling includes -1 and clamps at both declared ends");
        int travel=legacy.miniHeight-legacy.miniLength(600);
        check(legacy.miniOffset(-300,600)==0&&legacy.miniOffset(7000,600)==travel,"minimap covers the full declared scroll range");
        check(legacy.scrollFromMini(0,600)==-300&&legacy.scrollFromMini(travel,600)==7000,
            "minimap endpoints restore the exact signed scroll positions");
        boolean rejected=false;try{chart(7001);}catch(IllegalArgumentException expected){rejected=true;}
        check(rejected,"inverted scroll limits are rejected");
        System.out.println("PASS chart geometry and signed scroll checks");
    }
}
